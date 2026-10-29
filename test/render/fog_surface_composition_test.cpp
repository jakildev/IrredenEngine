#include <gtest/gtest.h>

#include <irreden/ir_render.hpp>

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

// The fog-composing surface-lit composites read the main canvas's observer
// snapshot, fog window and line-of-sight field at slots no other resource in
// those programs declares, and fog each surface-lit sample exactly once: the
// preserving fog kernel skips the samples the composite fogs, by the same
// predicate the lighting kernel skips them with.

namespace {

const std::filesystem::path kShaderDir = IR_TEST_RENDER_SHADER_DIR;

std::string readSource(const std::filesystem::path &path) {
    std::ifstream file(path);
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

// The wrapper with every `#include "…"` expanded once, as both backends'
// loaders do.
std::string resolveIncludes(const std::filesystem::path &path, std::set<std::string> &visited) {
    const std::string canonical = std::filesystem::weakly_canonical(path).string();
    if (!visited.insert(canonical).second) {
        return {};
    }
    std::istringstream lines(readSource(path));
    std::ostringstream out;
    const std::regex include(R"re(^\s*#include\s+"([^"]+)")re");
    std::string line;
    std::smatch match;
    while (std::getline(lines, line)) {
        if (std::regex_search(line, match, include)) {
            out << resolveIncludes(path.parent_path() / match[1].str(), visited) << "\n";
        } else {
            out << line << "\n";
        }
    }
    return out.str();
}

std::string resolveIncludes(const std::filesystem::path &path) {
    std::set<std::string> visited;
    return resolveIncludes(path, visited);
}

// `#define NAME <int>` in @p source, or -1.
int definedInt(const std::string &source, const std::string &name) {
    std::smatch match;
    if (!std::regex_search(source, match, std::regex("#define " + name + R"( (\d+))"))) {
        return -1;
    }
    return std::stoi(match[1].str());
}

// Metal entry-point parameters by `[[kind(index)]]`, each as its declaring
// text. Only the parameter lists name slots, so the whole source is scanned.
std::multimap<int, std::string> metalSlots(const std::string &source, const std::string &kind) {
    std::multimap<int, std::string> slots;
    const std::regex slot("([A-Za-z_][^,\\n(]*?)\\s*\\[\\[" + kind + R"(\((\d+)\)\]\])");
    for (std::sregex_iterator it(source.begin(), source.end(), slot), end; it != end; ++it) {
        slots.emplace(std::stoi((*it)[2].str()), (*it)[1].str());
    }
    return slots;
}

struct GlslWrapper {
    const char *file_;
    const char *fogMacro_;
};

constexpr GlslWrapper kGlslFogWrappers[] = {
    {"f_trixel_to_framebuffer_lit_voxels_fog.glsl", "IR_SURFACE_FOG"},
    {"f_peraxis_surface_lighting_fog.glsl", "IR_PER_AXIS_SURFACE_FOG"},
    {"f_peraxis_visible_lighting_fog.glsl", "IR_PER_AXIS_SURFACE_FOG"},
    {"f_peraxis_query_stats_fog.glsl", "IR_PER_AXIS_SURFACE_FOG"},
};

constexpr const char *kMetalFogWrappers[] = {
    "trixel_to_framebuffer_lit_voxels_fog.metal",
    "peraxis_surface_lighting_fog.metal",
    "peraxis_visible_lighting_fog.metal",
    "peraxis_query_stats_fog.metal",
};

TEST(FogSurfaceComposition, GlslFragmentsDeclareTheSnapshotAndTexturesAtTheBoundSlots) {
    for (const GlslWrapper &wrapper : kGlslFogWrappers) {
        const std::string source = resolveIncludes(kShaderDir / wrapper.file_);
        ASSERT_FALSE(source.empty()) << wrapper.file_;
        EXPECT_EQ(definedInt(source, wrapper.fogMacro_), 1) << wrapper.file_;
        EXPECT_EQ(
            definedInt(source, "IR_FOG_OBSERVER_BINDING"),
            static_cast<int>(IRRender::kBufferIndex_FogSurfaceObservers)
        ) << wrapper.file_;
        EXPECT_EQ(
            definedInt(source, "IR_FOG_GRID_SAMPLER_BINDING"),
            static_cast<int>(IRRender::kFogSurfaceWindowTextureUnit)
        ) << wrapper.file_;
        EXPECT_EQ(
            definedInt(source, "IR_FOG_LOS_SAMPLER_BINDING"),
            static_cast<int>(IRRender::kFogSurfaceLineOfSightTextureUnit)
        ) << wrapper.file_;
        // Every other uniform block and sampler stays off those slots.
        const std::regex block(R"(layout\s*\(\s*std140\s*,\s*binding\s*=\s*(\d+)\s*\))");
        const std::regex sampler(
            R"(layout\s*\(\s*binding\s*=\s*(\d+)\s*\)\s*uniform\s+[iu]?sampler)"
        );
        for (std::sregex_iterator it(source.begin(), source.end(), block), end; it != end; ++it) {
            EXPECT_NE(
                std::stoi((*it)[1].str()),
                static_cast<int>(IRRender::kBufferIndex_FogSurfaceObservers)
            ) << wrapper.file_
              << " declares another uniform block at the snapshot's slot";
        }
        for (std::sregex_iterator it(source.begin(), source.end(), sampler), end; it != end; ++it) {
            const int unit = std::stoi((*it)[1].str());
            EXPECT_NE(unit, static_cast<int>(IRRender::kFogSurfaceWindowTextureUnit))
                << wrapper.file_;
            EXPECT_NE(unit, static_cast<int>(IRRender::kFogSurfaceLineOfSightTextureUnit))
                << wrapper.file_;
        }
    }
}

TEST(FogSurfaceComposition, MetalFragmentsTakeEachFogSlotOnce) {
    const int observerSlot = static_cast<int>(IRRender::kBufferIndex_FogSurfaceObservers);
    for (const char *wrapper : kMetalFogWrappers) {
        const std::string source = resolveIncludes(kShaderDir / "metal" / wrapper);
        ASSERT_FALSE(source.empty()) << wrapper;
        const std::string fragment = source.substr(source.find("\nfragment "));
        const auto buffers = metalSlots(fragment, "buffer");
        const auto textures = metalSlots(fragment, "texture");
        ASSERT_EQ(buffers.count(observerSlot), 1u) << wrapper;
        EXPECT_NE(buffers.find(observerSlot)->second.find("FogObserverData"), std::string::npos)
            << wrapper;
        // Lighting keeps the compute passes' observer slot.
        ASSERT_EQ(buffers.count(static_cast<int>(IRRender::kBufferIndex_FogObservers)), 1u)
            << wrapper;
        EXPECT_NE(
            buffers.find(static_cast<int>(IRRender::kBufferIndex_FogObservers))
                ->second.find("FrameDataLightingToTrixel"),
            std::string::npos
        ) << wrapper;
        for (const auto &[unit, name] :
             {std::pair{IRRender::kFogSurfaceWindowTextureUnit, "canvasFogOfWar"},
              std::pair{IRRender::kFogSurfaceLineOfSightTextureUnit, "fogLineOfSight"}}) {
            ASSERT_EQ(textures.count(static_cast<int>(unit)), 1u) << wrapper << " " << name;
            EXPECT_NE(textures.find(static_cast<int>(unit))->second.find(name), std::string::npos)
                << wrapper;
        }
    }
}

// A sample is fogged by the preserving kernel or by the composite, never both,
// and never by neither: both skip predicates are the lighting kernel's.
TEST(FogSurfaceComposition, PreservingKernelSkipsExactlyTheSamplesTheCompositeFogs) {
    for (const char *path : {"c_fog_to_trixel_body.glsl", "metal/c_fog_to_trixel_body.metal"}) {
        const std::string kernel = readSource(kShaderDir / path);
        const std::size_t guard = kernel.find("#if IR_FOG_PRESERVE_CARDINAL_SURFACES");
        ASSERT_NE(guard, std::string::npos) << path;
        const std::size_t skip = kernel.find("isCardinalVoxelSurface(rawId)", guard);
        ASSERT_NE(skip, std::string::npos) << path;
        EXPECT_LT(skip, kernel.find("return;", guard)) << path;
        // The skip runs before the kernel's first colour write.
        const std::size_t firstWrite =
            kernel.find(path[0] == 'm' ? "trixelColors.write(" : "imageStore(trixelColors");
        ASSERT_NE(firstWrite, std::string::npos) << path;
        EXPECT_LT(skip, firstWrite) << path;
        EXPECT_NE(kernel.find("perAxisRoute == 0", guard), std::string::npos) << path;
    }
    for (const char *path :
         {"ir_trixel_to_framebuffer_body.glsl", "metal/ir_trixel_to_framebuffer_body.metal"}) {
        const std::string body = readSource(kShaderDir / path);
        const std::size_t gate = body.find("isCardinalVoxelSurface(cardinalRawId)");
        const std::size_t compose = body.find("fogComposeSurface(");
        ASSERT_NE(gate, std::string::npos) << path;
        ASSERT_NE(compose, std::string::npos) << path;
        EXPECT_LT(gate, compose) << path;
        // One composition per surface-lit sample.
        EXPECT_EQ(body.find("fogComposeSurface(", compose + 1), std::string::npos) << path;
    }
    for (const char *path :
         {"ir_peraxis_scatter_fragment_body.glsl",
          "metal/ir_peraxis_scatter_fragment_body.metal"}) {
        const std::string body = readSource(kShaderDir / path);
        const std::size_t compose = body.find("fogComposeSurface(");
        ASSERT_NE(compose, std::string::npos) << path;
        EXPECT_EQ(body.find("fogComposeSurface(", compose + 1), std::string::npos) << path;
    }
}

} // namespace
