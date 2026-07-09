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

#elif defined(IR_GRAPHICS_METAL)

// The Metal half of vehicle A (#1640). The OpenGL twin above has no analogous
// bug — GL writes real image atomics straight to the texture — so this fixture
// exists to reproduce the *Metal-only* gap headlessly: a non-main canvas's R32I
// distance texture, written by one in-tick compute dispatch, reads back as the
// clear value from a SECOND in-tick dispatch (engine/render/CLAUDE.md
// "Foreign-canvas R32I image reads in a second in-tick compute dispatch return
// empty on Metal (#1640)"). Metal has no windowless RenderDevice bring-up in the
// normal engine boot (the CAMetalLayer is window-bound), so the fixture uses
// bootstrapHeadlessRenderDevice() — device + command queue, no swapchain — then
// drives the real ShaderProgram / Texture2D / dispatchCompute path.

#include <irreden/render/buffer.hpp>
#include <irreden/render/ir_render_enums.hpp>
#include <irreden/render/metal/metal_runtime.hpp>
#include <irreden/render/render_device.hpp>
#include <irreden/render/shader.hpp>
#include <irreden/render/texture.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

// 1024 texels: every linear index stays below the clear sentinel 65535, so a
// missed write is unambiguously distinguishable from a correct one.
constexpr int kTexDim = 32;
constexpr int kTexelCount = kTexDim * kTexDim;
constexpr std::int32_t kEmptyDistanceEncoded = 65535; // kTrixelDistanceMaxDistance.
constexpr std::uint32_t kOutputBinding = 0;           // matches [[buffer(0)]] in c_r32i_read.

// Brings up a windowless Metal device for one test and skips cleanly when none
// is available (headless host without a GPU), mirroring the GL fixture's skip.
class MetalGpuComputeDispatchTest : public ::testing::Test {
  protected:
    void SetUp() override {
        device_ = IRRender::bootstrapHeadlessRenderDevice();
        if (device_ == nullptr) {
            GTEST_SKIP() << "No Metal device available — headless host without a GPU.";
        }
    }

    // Mirrors the GL fixture's per-test cleanup: without this, each SetUp's
    // initializeMetalRuntime reassigns the command queue + depth-stencil
    // states without releasing the previous test's, leaking them (and the
    // raw MTL::Device, which shutdownMetalRuntime does not own) every test
    // after the first.
    void TearDown() override {
        if (device_ == nullptr) {
            return;
        }
        MTL::Device *rawDevice = IRRender::metalDevice();
        IRRender::shutdownMetalRuntime();
        if (rawDevice != nullptr) {
            rawDevice->release();
        }
        device_ = nullptr;
    }

    IRRender::RenderDevice *device_ = nullptr;
};

// Positive control / oracle validation: with NO write dispatch, a read of the
// cleared texture must report exactly the clear sentinel. This proves the
// harness can distinguish a missed write from a correct one — i.e. a real
// second-dispatch read gap (#1640) would surface here as a failed EXPECT — and
// that clearTexImage lands on the command buffer before the read encoder.
TEST_F(MetalGpuComputeDispatchTest, ClearedTextureReadsBackAsClearSentinel) {
    using namespace IRRender;

    Texture2D distances{TextureKind::TEXTURE_2D, kTexDim, kTexDim, TextureFormat::R32I};
    const std::int32_t clearValue = kEmptyDistanceEncoded;
    device_->clearTexImage(&distances, 0, &clearValue);

    const std::vector<std::int32_t> outSeed(kTexelCount, -1);
    Buffer output{
        outSeed.data(),
        outSeed.size() * sizeof(std::int32_t),
        BUFFER_STORAGE_NONE,
        BufferTarget::SHADER_STORAGE,
        kOutputBinding
    };

    const std::string readPath = std::string(IR_TEST_GPU_SHADER_DIR) + "/c_r32i_read.glsl";
    ShaderProgram readProgram{std::vector{ShaderStage{readPath.c_str(), ShaderType::COMPUTE}}};

    readProgram.use();
    distances.bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
    output.bindBase(BufferTarget::SHADER_STORAGE, kOutputBinding);
    device_->dispatchCompute(kTexDim, kTexDim, 1);
    device_->finish();

    std::vector<std::int32_t> readback(kTexelCount, -2);
    output.getSubData(0, readback.size() * sizeof(std::int32_t), readback.data());

    std::size_t nonSentinel = 0;
    for (int i = 0; i < kTexelCount; ++i) {
        if (readback[i] != kEmptyDistanceEncoded) {
            ++nonSentinel;
        }
    }
    EXPECT_EQ(nonSentinel, 0u)
        << nonSentinel << " / " << kTexelCount
        << " texels did not read back the clear sentinel — clearTexImage or the "
           "access::read path is not behaving as the oracle assumes.";
}

TEST_F(MetalGpuComputeDispatchTest, SecondDispatchSeesFirstDispatchDistanceWrites) {
    using namespace IRRender;

    Texture2D distances{TextureKind::TEXTURE_2D, kTexDim, kTexDim, TextureFormat::R32I};

    // Clear to the empty sentinel first, so a read that misses the write reports
    // exactly 65535 (the documented #1640 symptom) rather than undefined memory.
    const std::int32_t clearValue = kEmptyDistanceEncoded;
    device_->clearTexImage(&distances, 0, &clearValue);

    // Seed with -1 (never a valid texel value) so a read dispatch that fails to
    // run at all is distinguishable from one that reads the clear sentinel.
    const std::vector<std::int32_t> outSeed(kTexelCount, -1);
    Buffer output{
        outSeed.data(),
        outSeed.size() * sizeof(std::int32_t),
        BUFFER_STORAGE_NONE,
        BufferTarget::SHADER_STORAGE,
        kOutputBinding
    };

    const std::string writePath = std::string(IR_TEST_GPU_SHADER_DIR) + "/c_r32i_write.glsl";
    const std::string readPath = std::string(IR_TEST_GPU_SHADER_DIR) + "/c_r32i_read.glsl";
    ShaderProgram writeProgram{std::vector{ShaderStage{writePath.c_str(), ShaderType::COMPUTE}}};
    ShaderProgram readProgram{std::vector{ShaderStage{readPath.c_str(), ShaderType::COMPUTE}}};

    // Dispatch 1 — populate the distance texture (mirrors c_voxel_to_trixel_stage_2's
    // access::write store, the step at which a canvas's distances become canonical).
    writeProgram.use();
    distances.bindAsImage(0, TextureAccess::WRITE_ONLY, TextureFormat::R32I);
    device_->dispatchCompute(kTexDim, kTexDim, 1);

    // Dispatch 2 — a fresh compute encoder on the SAME command buffer reads the
    // texture back through the exact access::read path c_bake_sun_shadow_map uses.
    readProgram.use();
    distances.bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
    output.bindBase(BufferTarget::SHADER_STORAGE, kOutputBinding);
    device_->dispatchCompute(kTexDim, kTexDim, 1);

    device_->finish(); // commit + wait so the SSBO readback observes GPU results.

    std::vector<std::int32_t> readback(kTexelCount, -2);
    output.getSubData(0, readback.size() * sizeof(std::int32_t), readback.data());

    std::size_t emptyReads = 0; // read back the clear sentinel — the #1640 gap.
    std::size_t wrongReads = 0; // neither the written index nor the sentinel.
    for (int i = 0; i < kTexelCount; ++i) {
        if (readback[i] == kEmptyDistanceEncoded) {
            ++emptyReads;
        } else if (readback[i] != i) {
            ++wrongReads;
        }
    }
    EXPECT_EQ(emptyReads, 0u)
        << emptyReads << " / " << kTexelCount
        << " texels read back the clear sentinel (65535): the second in-tick "
           "dispatch did not see the first dispatch's distance writes (#1640).";
    EXPECT_EQ(wrongReads, 0u) << wrongReads
                              << " texels read back neither their written linear index "
                                 "nor the clear sentinel.";
}

} // namespace

#else // other backend (e.g. Vulkan)

// Keep a registered placeholder so the suite still appears (as skipped) rather
// than silently vanishing from the cross-backend test inventory.
TEST(GpuComputeDispatchTest, SkippedOnUnsupportedBackend) {
    GTEST_SKIP() << "Headless GPU compute test targets the OpenGL and Metal backends.";
}

#endif // IR_GRAPHICS_OPENGL / IR_GRAPHICS_METAL
