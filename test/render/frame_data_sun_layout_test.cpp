#include <gtest/gtest.h>

#include <irreden/render/ir_render_types.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

// Runtime tripwire mirroring the compile-time static_asserts in ir_render_types.hpp (std140 leading edge).
TEST(FrameDataSunLayout, MatchesStd140Packing) {
    EXPECT_EQ(sizeof(IRRender::FrameDataSun), 160u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunDirection_), 0u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunIntensity_), 16u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunAmbient_), 20u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, shadowsEnabled_), 24u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, aoEnabled_), 28u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunBasisU_), 32u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunBasisV_), 48u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunBufferOriginUV_), 64u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunBufferTexelSize_), 72u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, cascadeOriginUV_0_), 80u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, cascadeTexelSize_0_), 88u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, cascadeOriginUV_1_), 96u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, cascadeTexelSize_1_), 104u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, cascadeSplitDepth_), 112u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, cascadeCount_), 116u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunSplatMaxTexels_), 120u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunMaxShadowThrow_), 124u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, sunCasterViewToWorld_), 128u);
    EXPECT_EQ(offsetof(IRRender::FrameDataSun, fogCeilingEnabled_), 144u);
}

TEST(FrameDataSunLayout, DefaultCasterBasisIsIdentity) {
    const IRRender::FrameDataSun frame;
    EXPECT_EQ(frame.sunCasterViewToWorld_, IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(frame.fogCeilingEnabled_, IRMath::ivec4(0));
}

namespace {

// CPU member order of FrameDataSun, trailing underscore dropped.
constexpr std::array<const char *, 19> kFrameDataSunMembers = {
    "sunDirection",
    "sunIntensity",
    "sunAmbient",
    "shadowsEnabled",
    "aoEnabled",
    "sunBasisU",
    "sunBasisV",
    "sunBufferOriginUV",
    "sunBufferTexelSize",
    "cascadeOriginUV_0",
    "cascadeTexelSize_0",
    "cascadeOriginUV_1",
    "cascadeTexelSize_1",
    "cascadeSplitDepth",
    "cascadeCount",
    "sunSplatMaxTexels",
    "sunMaxShadowThrow",
    "sunCasterViewToWorld",
    "fogCeilingEnabled",
};

std::vector<std::string> frameDataSunMirrorMembers(const std::string &source, std::size_t open) {
    const std::size_t close = source.find('}', open);
    std::string body = source.substr(open + 1, close - open - 1);
    body = std::regex_replace(body, std::regex(R"(//[^\n]*)"), "");
    std::vector<std::string> members;
    std::stringstream statements(body);
    std::string statement;
    const std::regex trailingName(R"((\w+)\s*(\[[^\]]*\])?\s*$)");
    while (std::getline(statements, statement, ';')) {
        std::smatch match;
        if (std::regex_search(statement, match, trailingName)) {
            members.push_back(match[1].str());
        }
    }
    return members;
}

} // namespace

// A per-kernel mirror may stop early, but it must never skip a member: a
// skipped field shifts every later one to a different std140 offset.
TEST(FrameDataSunLayout, EveryShaderMirrorIsAPrefixOfTheCpuMemberOrder) {
    const std::regex blockOpen(R"((uniform|struct)\s+FrameDataSun\s*\{)");
    const std::filesystem::path shaderDir{IR_TEST_RENDER_SHADER_DIR};
    int mirrors = 0;
    int fogCeilingMirrors = 0;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(shaderDir)) {
        const std::string extension = entry.path().extension().string();
        if (extension != ".glsl" && extension != ".metal") {
            continue;
        }
        std::ifstream file(entry.path());
        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string source = buffer.str();
        const auto first = std::sregex_iterator(source.begin(), source.end(), blockOpen);
        for (auto it = first; it != std::sregex_iterator(); ++it) {
            SCOPED_TRACE(entry.path().string());
            const std::size_t open = static_cast<std::size_t>(it->position() + it->length() - 1);
            const std::vector<std::string> members = frameDataSunMirrorMembers(source, open);
            ASSERT_LE(members.size(), kFrameDataSunMembers.size());
            for (std::size_t i = 0; i < members.size(); ++i) {
                EXPECT_EQ(members[i], kFrameDataSunMembers[i]) << "member " << i;
            }
            ++mirrors;
            if (!members.empty() && members.back() == "fogCeilingEnabled") {
                ++fogCeilingMirrors;
            }
        }
    }
    // ir_sun_shadow_sample (both dialects), three GLSL bake kernels and the
    // Metal main-canvas bake carry the full block.
    EXPECT_GE(fogCeilingMirrors, 6);
    EXPECT_GT(mirrors, fogCeilingMirrors);
}
