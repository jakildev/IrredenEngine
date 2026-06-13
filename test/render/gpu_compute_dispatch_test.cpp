// Vehicle A (#1771, epic #1766): the engine's first *headless GPU* unit-test
// category. Unlike the rest of test/render/* (pure CPU math / layout), this
// stands up a hidden OpenGL 4.5 core context, compiles a real engine compute
// shader, dispatches it, reads the output SSBO back to the CPU, and asserts a
// shader-level invariant directly — no full-frame screenshot, no pixel diff.
//
// Invariant under test: the sun-shadow CLEAR kernel
// (engine/render/src/shaders/c_clear_sun_shadow_map.glsl) fills its depth SSBO
// with the lit-sentinel 0xFFFFFFFF. That is the documented precondition for the
// BAKE pass — a stale texel reads back as a false shadow — so asserting it here
// is the structural property that defines correct clear behaviour: robust to FP
// jitter and backend-agnostic in intent. Seeding the buffer with zeros first
// means a dispatch that never runs fails the assertion, so the test proves the
// GPU actually wrote.
//
// Future children of #1766 assert heavier pipeline kernels (resolve footprint
// density, sun-bake non-emptiness) through this same dispatch + readback harness
// — the reusable piece is the hidden-context fixture, not this one shader.
//
// The fixture GTEST_SKIPs when a GL 4.5 context cannot be created (headless CI
// with no display / no GPU), so the always-run CPU suite stays green there.

#include <gtest/gtest.h>

#if defined(IR_GRAPHICS_OPENGL)

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <irreden/ir_math.hpp>
#include <irreden/render/buffer.hpp>
#include <irreden/render/ir_gl_api.hpp>
#include <irreden/render/ir_render_enums.hpp>
#include <irreden/render/shader.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

// Mirror the compile-time constants baked into c_clear_sun_shadow_map.glsl. A
// divergence here would surface as uncleared tail texels in the assertion.
constexpr int kSunShadowMapDim = 1024;
constexpr int kSunShadowCascadeCount = 2;
constexpr int kTotalTexels = kSunShadowMapDim * kSunShadowMapDim * kSunShadowCascadeCount;
constexpr std::uint32_t kBindingSunShadowDepthMap = 28; // std430 binding in the shader
constexpr std::uint32_t kLitSentinel = 0xFFFFFFFFu;
constexpr int kLocalSize = 16; // local_size_x / local_size_y in the shader

// Brings up a hidden OpenGL 4.5 core context for one test, mirroring the
// engine's own window hints (ir_glfw_window.hpp). Skips cleanly when no context
// is obtainable so this suite is a no-op on display-less hosts.
class GpuComputeDispatchTest : public ::testing::Test {
  protected:
    void SetUp() override {
        if (!glfwInit()) {
            GTEST_SKIP() << "glfwInit failed — no display / headless host without a GPU.";
        }
        glfwInitialized_ = true;
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // headless: never shown
        window_ = glfwCreateWindow(16, 16, "ir-gpu-compute-test", nullptr, nullptr);
        if (window_ == nullptr) {
            GTEST_SKIP() << "OpenGL 4.5 core context unavailable on this host.";
        }
        glfwMakeContextCurrent(window_);
    }

    void TearDown() override {
        if (window_ != nullptr) {
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }
        if (glfwInitialized_) {
            glfwTerminate();
            glfwInitialized_ = false;
        }
    }

    GLFWwindow *window_ = nullptr;
    bool glfwInitialized_ = false;
};

TEST_F(GpuComputeDispatchTest, ClearSunShadowKernelFillsBufferWithLitSentinel) {
    using namespace IRRender;

    const std::string shaderPath =
        std::string(IR_TEST_RENDER_SHADER_DIR) + "/c_clear_sun_shadow_map.glsl";
    ShaderProgram clearProgram{std::vector{ShaderStage{shaderPath.c_str(), ShaderType::COMPUTE}}};

    // Seed with the non-sentinel value so the assertion can only pass if the
    // GPU wrote every texel — a silently-skipped dispatch leaves zeros behind.
    const std::vector<std::uint32_t> seed(kTotalTexels, 0u);
    Buffer depthMap{
        seed.data(),
        seed.size() * sizeof(std::uint32_t),
        BUFFER_STORAGE_NONE,
        BufferTarget::SHADER_STORAGE,
        kBindingSunShadowDepthMap
    };

    clearProgram.use();
    depthMap.bindBase(BufferTarget::SHADER_STORAGE, kBindingSunShadowDepthMap);

    // The shader linear-indexes as gid.y * kSunShadowMapDim + gid.x, so cover
    // gid.x in [0, dim) and gid.y in [0, dim * cascades). divCeil mirrors the
    // production clear dispatch in system_bake_sun_shadow_map.hpp.
    const int groupsX = IRMath::divCeil(kSunShadowMapDim, kLocalSize);
    const int groupsY = IRMath::divCeil(kSunShadowMapDim * kSunShadowCascadeCount, kLocalSize);
    ENG_API->glDispatchCompute(groupsX, groupsY, 1);

    // Make the shader's SSBO writes visible to the client-side readback, then
    // block until the dispatch has completed (this is a test, not a hot path).
    ENG_API->glMemoryBarrier(GL_ALL_BARRIER_BITS);
    ENG_API->glFinish();

    std::vector<std::uint32_t> readback(kTotalTexels, 0u);
    depthMap.getSubData(0, readback.size() * sizeof(std::uint32_t), readback.data());

    std::size_t mismatches = 0;
    for (std::uint32_t texel : readback) {
        if (texel != kLitSentinel) {
            ++mismatches;
        }
    }
    EXPECT_EQ(mismatches, 0u) << mismatches << " / " << kTotalTexels
                              << " texels were not cleared to the lit-sentinel 0xFFFFFFFF.";
    // Explicit boundary checks: first and last texel of the covered range.
    EXPECT_EQ(readback.front(), kLitSentinel);
    EXPECT_EQ(readback.back(), kLitSentinel);
}

} // namespace

#else // !IR_GRAPHICS_OPENGL

// On Metal / Vulkan builds the GL dispatch + readback harness does not apply.
// Keep a registered placeholder so the suite still appears (as skipped) rather
// than silently vanishing from the cross-backend test inventory.
TEST(GpuComputeDispatchTest, SkippedOnNonOpenGLBackend) {
    GTEST_SKIP() << "Headless GPU compute test targets the OpenGL backend only.";
}

#endif // IR_GRAPHICS_OPENGL
