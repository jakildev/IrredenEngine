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

// #2488: RenderDevice::resolveImageAtomicScratch materializes the R32I
// image-atomic scratch buffer into the texture it mirrors, so a later
// sampler / access::read pass sees depth the atomic passes wrote. In the
// pipeline that is what carries the shadow-feeder ring into trixelDistances
// on Metal; here it is exercised directly against the scratch.
//
// An R32I texture pairs its scratch at whichever comes first, bindAsImage
// (MetalTexture2DImpl::bindImage) or clearTexImage — both route through
// ensureImageAtomicScratchBuffer. Binding FIRST is a precondition of this test,
// not incidental setup: it fixes the order so the clear's mirror seeds the
// sentinel baseline the CPU seed below then overwrites. The seed pattern is
// written straight into the scratch's shared-storage contents, standing in for
// the atomic-min writes the stage-1 dispatches perform.
TEST_F(MetalGpuComputeDispatchTest, ResolveImageAtomicScratchLandsScratchInTexture) {
    using namespace IRRender;

    Texture2D distances{TextureKind::TEXTURE_2D, kTexDim, kTexDim, TextureFormat::R32I};

    // Pair the scratch, then clear the TEXTURE to the empty sentinel.
    //
    // The finish() is load-bearing, not tidiness. clearTexImage ENQUEUES two
    // blits — one into the texture, one mirroring the clear into the scratch —
    // and the CPU seed below writes shared-storage memory immediately. Without a
    // sync the GPU runs the mirror after the seed and overwrites the pattern
    // with sentinels, so the resolve faithfully copies sentinels back and the
    // assertion fails for a reason that has nothing to do with the resolve.
    // (Production never has this hazard: there the scratch is written by the
    // stage-1 atomics, so producer and consumer are both on the command buffer
    // in encoder order.)
    distances.bindAsImage(0, TextureAccess::READ_WRITE, TextureFormat::R32I);
    const std::int32_t clearValue = kEmptyDistanceEncoded;
    device_->clearTexImage(&distances, 0, &clearValue);
    device_->finish();

    auto *scratch =
        lookupImageAtomicScratchBuffer(static_cast<MTL::Texture *>(distances.getNativeTexture()));
    ASSERT_NE(scratch, nullptr) << "bindAsImage on an R32I texture must pair a scratch buffer.";

    // Distinguishable from both the clear sentinel (65535) and the -1/-2
    // readback seeds, and distinct per texel so a partial blit is visible.
    auto *scratchTexels = static_cast<std::int32_t *>(scratch->contents());
    for (int i = 0; i < kTexelCount; ++i) {
        scratchTexels[i] = i;
    }

    const std::vector<std::int32_t> outSeed(kTexelCount, -1);
    Buffer output{
        outSeed.data(),
        outSeed.size() * sizeof(std::int32_t),
        BUFFER_STORAGE_NONE,
        BufferTarget::SHADER_STORAGE,
        kOutputBinding
    };

    device_->resolveImageAtomicScratch(&distances);

    const std::string readPath = std::string(IR_TEST_GPU_SHADER_DIR) + "/c_r32i_read.glsl";
    ShaderProgram readProgram{std::vector{ShaderStage{readPath.c_str(), ShaderType::COMPUTE}}};
    readProgram.use();
    distances.bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
    output.bindBase(BufferTarget::SHADER_STORAGE, kOutputBinding);
    device_->dispatchCompute(kTexDim, kTexDim, 1);
    device_->finish();

    std::vector<std::int32_t> readback(kTexelCount, -2);
    output.getSubData(0, readback.size() * sizeof(std::int32_t), readback.data());

    std::size_t sentinelReads = 0; // the resolve never reached this texel.
    std::size_t wrongReads = 0;    // neither the seeded value nor the sentinel.
    for (int i = 0; i < kTexelCount; ++i) {
        if (readback[i] == kEmptyDistanceEncoded) {
            ++sentinelReads;
        } else if (readback[i] != i) {
            ++wrongReads;
        }
    }
    EXPECT_EQ(sentinelReads, 0u)
        << sentinelReads << " / " << kTexelCount
        << " texels still read the clear sentinel — resolveImageAtomicScratch did not "
           "materialize the scratch into the texture (#2488).";
    EXPECT_EQ(wrongReads, 0u) << wrongReads
                              << " texels read back neither their seeded "
                                 "scratch value nor the clear sentinel.";
}

// Negative control for the test above, differing from it in exactly one line:
// no resolveImageAtomicScratch call. The texture must still read back as the
// clear sentinel despite the scratch holding the same pattern — i.e. the
// assertion above CAN fail, and it is the resolve that makes it pass rather
// than any incidental coupling between the scratch and the texture.
//
// It carries the same clear/seed finish() as its sibling for a specific reason:
// without it the clear's mirror blit wipes the seed, and this control passes
// because the scratch holds sentinels rather than because the resolve is
// absent — a green control that is blind to the property it exists to pin.
TEST_F(MetalGpuComputeDispatchTest, SeededScratchAloneDoesNotReachTheTexture) {
    using namespace IRRender;

    Texture2D distances{TextureKind::TEXTURE_2D, kTexDim, kTexDim, TextureFormat::R32I};

    distances.bindAsImage(0, TextureAccess::READ_WRITE, TextureFormat::R32I);
    const std::int32_t clearValue = kEmptyDistanceEncoded;
    device_->clearTexImage(&distances, 0, &clearValue);
    device_->finish();

    auto *scratch =
        lookupImageAtomicScratchBuffer(static_cast<MTL::Texture *>(distances.getNativeTexture()));
    ASSERT_NE(scratch, nullptr) << "bindAsImage on an R32I texture must pair a scratch buffer.";

    auto *scratchTexels = static_cast<std::int32_t *>(scratch->contents());
    for (int i = 0; i < kTexelCount; ++i) {
        scratchTexels[i] = i;
    }

    const std::vector<std::int32_t> outSeed(kTexelCount, -1);
    Buffer output{
        outSeed.data(),
        outSeed.size() * sizeof(std::int32_t),
        BUFFER_STORAGE_NONE,
        BufferTarget::SHADER_STORAGE,
        kOutputBinding
    };

    // No resolveImageAtomicScratch here — that is the whole control.

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
        << " texels read back a non-sentinel value with no resolve call — the sibling "
           "test's assertion would pass without resolveImageAtomicScratch doing anything.";
}

// First-tick arm: replays the PRODUCTION order (clear -> bind -> resolve),
// which is the reverse of the two tests above. They bind first, which pairs the
// scratch before clearTexImage runs, so the clear's mirror establishes the
// sentinel baseline they then assume — structurally unable to observe a canvas's
// very first tick. The tick orders it the other way round: the per-frame clear
// (clearCanvasAndDistances -> clearTexImage, system_voxel_to_trixel.hpp) runs
// well before stage 1's first bindAsImage on the distance texture.
//
// This differs from ClearedTextureReadsBackAsClearSentinel by exactly one line —
// the resolve call — so a failure here is attributable to the resolve reading a
// scratch the clear never seeded, and nothing else. A freshly created scratch is
// zero-filled (metal_runtime.cpp), and 0 encodes the NEAREST depth rather than
// "empty", so an unseeded resolve would stamp a solid surface across the whole
// canvas for that tick. What keeps it seeded is clearTexImage ensuring (not
// merely looking up) the scratch, which is what makes the "mirrors the clear
// unconditionally" claim at sun_shadow_constants.hpp hold on the first tick.
TEST_F(MetalGpuComputeDispatchTest, ResolveOnFirstTickLeavesTheClearSentinel) {
    using namespace IRRender;

    Texture2D distances{TextureKind::TEXTURE_2D, kTexDim, kTexDim, TextureFormat::R32I};

    // Production order: clear BEFORE any bindAsImage pairs a scratch.
    const std::int32_t clearValue = kEmptyDistanceEncoded;
    device_->clearTexImage(&distances, 0, &clearValue);

    // Stage 1's first image bind on this texture. No dispatch follows it here —
    // an atomic-min pass would only lower texels further, so the empty-canvas
    // case is the one that isolates the clear's own baseline.
    distances.bindAsImage(0, TextureAccess::READ_WRITE, TextureFormat::R32I);

    // No finish() anywhere in this test, deliberately: unlike its two siblings
    // there is no CPU seed to race, so every write is on the command buffer in
    // encoder order exactly as production has it.
    device_->resolveImageAtomicScratch(&distances);

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

    std::size_t zeroReads = 0;   // the unseeded-scratch symptom: nearest depth.
    std::size_t nonSentinel = 0; // any other departure from the clear value.
    for (int i = 0; i < kTexelCount; ++i) {
        if (readback[i] == kEmptyDistanceEncoded) {
            continue;
        }
        ++nonSentinel;
        if (readback[i] == 0) {
            ++zeroReads;
        }
    }
    EXPECT_EQ(zeroReads, 0u)
        << zeroReads << " / " << kTexelCount
        << " texels read back 0 (nearest depth) after a first-tick resolve — the clear "
           "did not seed the scratch, so the resolve blitted a zero-filled buffer over "
           "the 65535 sentinel (#2488).";
    EXPECT_EQ(nonSentinel, 0u)
        << nonSentinel << " / " << kTexelCount
        << " texels departed from the clear sentinel after a resolve with no atomic "
           "pass in between.";
}

} // namespace

#else // other backend (e.g. Vulkan)

// Keep a registered placeholder so the suite still appears (as skipped) rather
// than silently vanishing from the cross-backend test inventory.
TEST(GpuComputeDispatchTest, SkippedOnUnsupportedBackend) {
    GTEST_SKIP() << "Headless GPU compute test targets the OpenGL and Metal backends.";
}

#endif // IR_GRAPHICS_OPENGL / IR_GRAPHICS_METAL

#if defined(IR_GRAPHICS_OPENGL) || defined(IR_GRAPHICS_METAL)
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <algorithm>
#include <array>

namespace {
#if defined(IR_GRAPHICS_METAL)
using PositionUploadTest = MetalGpuComputeDispatchTest;
#else
using PositionUploadTest = GpuComputeDispatchTest;
#endif

TEST_F(PositionUploadTest, OverflowSortHandlesFirstPopulationAndCountTransitions) {
    using namespace IRRender;
    using Axes = IRComponents::C_PerAxisTrixelCanvases;
    constexpr std::uint32_t cap = 1u << 19;
    constexpr std::uint32_t ctrl = 64;
    constexpr std::uint32_t entries = ctrl + Axes::kOverflowControlUints;
    using Record = std::array<std::uint32_t, 3>;
    std::vector<std::uint32_t> words(entries + cap * 3, 0xA5A5A5A5u);
    Buffer scratch(words.data(), words.size() * sizeof(std::uint32_t), BUFFER_STORAGE_DYNAMIC);
    FrameDataVoxelToCanvas frame{};
    frame.overflowScratchLayout_ = IRMath::ivec4(0, ctrl, entries, cap);
    Buffer uniform(&frame, sizeof(frame), BUFFER_STORAGE_DYNAMIC);
    const std::string path =
        std::string(IR_TEST_RENDER_SHADER_DIR) + "/c_per_axis_overflow_sort.glsl";
    ShaderProgram program{std::vector{ShaderStage{path.c_str(), ShaderType::COMPUTE}}};
    const auto recordLess = [](const Record &a, const Record &b) {
        if (a[0] != b[0])
            return a[0] < b[0];
        if (a[2] != b[2])
            return a[2] < b[2];
        return a[1] < b[1];
    };
    struct SortCase {
        std::uint32_t count;
        std::uint32_t laggedCount;
        bool fullySorted;
    };
    for (const SortCase sortCase : {
             SortCase{0u, 0u, true},
             SortCase{2048u, 0u, true},
             SortCase{4097u, 0u, false},
             SortCase{4097u, 4097u, true},
             SortCase{1u, 4097u, true},
             SortCase{0u, 1u, true},
             SortCase{262145u, 262145u, true},
         }) {
        const std::uint32_t count = sortCase.count;
        SCOPED_TRACE(
            ::testing::Message() << "count=" << count << " lagged=" << sortCase.laggedCount
        );
        words[ctrl + 1] = count;
        std::vector<Record> expected;
        expected.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            Record record{(count - i) % 37u, (i * 97u) % 65521u, (i * 31u) % 101u};
            expected.push_back(record);
            for (int word = 0; word < 3; ++word)
                words[entries + i * 3 + word] = record[word];
        }
        std::sort(expected.begin(), expected.end(), recordLess);
        scratch.subData(0, words.size() * sizeof(std::uint32_t), words.data());
        scratch.bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_PerAxisResolveScratch);
        uniform.bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataVoxelToCanvas);
        program.use();
        auto step = [&](int mode,
                        std::uint32_t k,
                        std::uint32_t lo,
                        std::uint32_t hi,
                        std::uint32_t command) {
            frame.overflowSortStep_ = IRMath::ivec4(mode, k, lo, hi);
            uniform.subData(0, sizeof(frame), &frame);
            const auto offset = static_cast<std::ptrdiff_t>(
                                    ctrl + Axes::kOverflowSortArgsBaseUints +
                                    command * Axes::kOverflowSortCommandUints
                                ) *
                                sizeof(std::uint32_t);
#if defined(IR_GRAPHICS_METAL)
            if (mode == 3)
                device_->dispatchCompute(1, 1, 1);
            else
                device_->dispatchComputeIndirect(&scratch, offset);
            device_->memoryBarrier(BarrierType::SHADER_STORAGE);
            device_->memoryBarrier(BarrierType::COMMAND);
#else
            if (mode == 3)
                ENG_API->glDispatchCompute(1, 1, 1);
            else {
                ENG_API->glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, scratch.getHandle());
                ENG_API->glDispatchComputeIndirect(offset);
            }
            ENG_API->glMemoryBarrier(GL_ALL_BARRIER_BITS);
#endif
        };
        step(3, 0, 0, 0, 0);
        step(0, 0, 0, 0, 0);
        step(1, 0, 0, 0, 1);
        const auto dispatchSpan =
            IRSystem::detail::overflowSortDispatchSpan(sortCase.laggedCount, cap);
        std::uint32_t mergeDispatches = 0;
        IRSystem::detail::forEachOverflowSortMergeStep(
            dispatchSpan,
            [&](std::uint32_t k, std::uint32_t pLo, std::uint32_t pHi, std::uint32_t commandIndex) {
                step(2, k, pLo, pHi, commandIndex);
                ++mergeDispatches;
            }
        );
        if (sortCase.laggedCount == 0u) {
            EXPECT_EQ(dispatchSpan, 1u << Axes::kOverflowSortBlockBits);
            EXPECT_EQ(mergeDispatches, 0u);
        }
#if defined(IR_GRAPHICS_METAL)
        device_->finish();
#else
        ENG_API->glFinish();
#endif
        std::vector<std::uint32_t> actual(words.size());
        scratch.getSubData(0, actual.size() * sizeof(std::uint32_t), actual.data());
        std::vector<Record> observed;
        observed.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            observed.push_back(
                Record{
                    actual[entries + i * 3],
                    actual[entries + i * 3 + 1],
                    actual[entries + i * 3 + 2]
                }
            );
        }
        if (sortCase.fullySorted)
            EXPECT_EQ(observed, expected);
        else {
            std::sort(observed.begin(), observed.end(), recordLess);
            EXPECT_EQ(observed, expected);
        }
        for (std::uint32_t i = 0; i < 8; ++i)
            EXPECT_EQ(actual[ctrl + i], words[ctrl + i]);
        const auto args = ctrl + Axes::kOverflowSortArgsBaseUints;
        EXPECT_LE(actual[args], 1024u);
        EXPECT_EQ(actual[args + 1] == 0, count == 0);
        if (count == 262145u)
            EXPECT_EQ(actual[args + 1], 2u);
        if (count == 0) {
            for (std::uint32_t command = 0; command < Axes::kOverflowSortCommandCount; ++command)
                EXPECT_EQ(actual[args + command * Axes::kOverflowSortCommandUints + 1], 0u);
        }
    }
}

TEST_F(PositionUploadTest, OverflowLightingDispatchUsesCurrentCountAndPreservesDrawArguments) {
    using namespace IRRender;
    const std::string path =
        std::string(IR_TEST_RENDER_SHADER_DIR) + "/c_per_axis_cell_finalize.glsl";
    ShaderProgram program{std::vector{ShaderStage{path.c_str(), ShaderType::COMPUTE}}};
    struct Case {
        std::uint32_t count;
        std::uint32_t groupsX;
        std::uint32_t groupsY;
    };
    // Nonempty-to-empty and X-to-Y spill transitions must overwrite stale args.
    const Case cases[] = {
        {8388608, 1024, 128},
        {0, 1, 0},
        {1, 1, 1},
        {64, 1, 1},
        {65, 2, 1},
        {65536, 1024, 1},
        {65537, 1024, 2},
        {0, 1, 0}
    };
    constexpr std::size_t stride = kPerAxisCellIndirectStrideBytes / sizeof(std::uint32_t);
    std::vector<std::uint32_t> expected(stride * 3, 0xA5A5A5A5u);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        expected[axis * stride + 1] = static_cast<std::uint32_t>(axis * 257);
    }
    Buffer indirect(
        expected.data(),
        expected.size() * sizeof(std::uint32_t),
        BUFFER_STORAGE_DYNAMIC
    );
    std::vector<std::uint32_t> control(stride * 2, 0xDEADBEEFu);
    Buffer scratch(control.data(), control.size() * sizeof(std::uint32_t), BUFFER_STORAGE_DYNAMIC);
    for (const auto &entry : cases) {
        SCOPED_TRACE(entry.count);
        control[stride + 1] = entry.count;
        scratch.subData(0, control.size() * sizeof(std::uint32_t), control.data());
        scratch.bindRange(
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_PerAxisResolveScratch,
            kPerAxisCellIndirectStrideBytes,
            kPerAxisCellIndirectStrideBytes
        );
        indirect.bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_PerAxisCellIndirect);
        program.use();
#if defined(IR_GRAPHICS_METAL)
        device_->dispatchCompute(3, 1, 1);
        device_->finish();
#else
        ENG_API->glDispatchCompute(3, 1, 1);
        ENG_API->glMemoryBarrier(GL_ALL_BARRIER_BITS);
        ENG_API->glFinish();
#endif
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const auto base =
                axis * stride + kPerAxisCellDispatchArgsOffsetBytes / sizeof(std::uint32_t);
            expected[base] = axis == 0 ? 1 : static_cast<std::uint32_t>(axis + 1);
            expected[base + 1] = axis == 0 ? 0 : 1;
            expected[base + 2] = 1;
            expected[base + 3] = static_cast<std::uint32_t>(axis * 257);
        }
        const auto base = kOverflowLightingDispatchArgsOffsetBytes / sizeof(std::uint32_t);
        expected[base] = entry.groupsX;
        expected[base + 1] = entry.groupsY;
        expected[base + 2] = 1;
        expected[base + 3] = entry.count;
        std::vector<std::uint32_t> actual(expected.size());
        indirect.getSubData(0, actual.size() * sizeof(std::uint32_t), actual.data());
        EXPECT_EQ(actual, expected);
        std::vector<std::uint32_t> actualControl(control.size());
        scratch.getSubData(0, actualControl.size() * sizeof(std::uint32_t), actualControl.data());
        EXPECT_EQ(actualControl, control);
    }
}

TEST_F(PositionUploadTest, ScatterCoverageCodesSurviveDepthQuantization) {
    using namespace IRRender;
    const std::uint32_t bands[] = {0u, 1u, 131071u, 262143u, 262144u, 262145u, 524286u, 524287u};
    std::vector<float> values(256, -1.0f);
    Buffer output{
        values.data(),
        values.size() * sizeof(float),
        BUFFER_STORAGE_NONE,
        BufferTarget::SHADER_STORAGE,
        0
    };
    const std::string path = std::string(IR_TEST_GPU_SHADER_DIR) + "/c_scatter_depth_probe.glsl";
    ShaderProgram program{std::vector{ShaderStage{path.c_str(), ShaderType::COMPUTE}}};
    program.use();
    output.bindBase(BufferTarget::SHADER_STORAGE, 0);
#if defined(IR_GRAPHICS_METAL)
    device_->dispatchCompute(256, 1, 1);
    device_->finish();
#else
    ENG_API->glDispatchCompute(256, 1, 1);
    ENG_API->glMemoryBarrier(GL_ALL_BARRIER_BITS);
    ENG_API->glFinish();
#endif
    output.getSubData(0, values.size() * sizeof(float), values.data());
    for (std::uint32_t i = 0; i < values.size(); ++i) {
        const std::uint32_t expected = bands[i >> 5u] * 32u + (i & 31u);
        ASSERT_GE(values[i], 0.0f);
        ASSERT_LE(values[i], 1.0f);
        const auto depth24 = static_cast<std::uint32_t>(double(values[i]) * 16777215.0 + 0.5);
        EXPECT_EQ(depth24, expected) << "code " << expected;
        if (i != 0) {
            EXPECT_GT(values[i], values[i - 1]) << "float depth code " << expected;
        }
    }
    EXPECT_EQ(values.front(), 0.0f);
    EXPECT_EQ(values.back(), 1.0f);
}

TEST_F(PositionUploadTest, SaturatedQueuePreservesGpuOwnedPositions) {
    using namespace IRRender;
    using namespace IRComponents;
    using namespace IRMath;
#if defined(IR_GRAPHICS_METAL)
    const std::vector<bool> queuedWrites{false, true};
#else
    const std::vector<bool> queuedWrites{false};
#endif
    for (const bool queuedGpuWrite : queuedWrites) {
        SCOPED_TRACE(queuedGpuWrite);
        for (const unsigned gpuMask : {0u, 0x0Au, 0xFFu}) {
            SCOPED_TRACE(gpuMask);
            constexpr std::size_t slotCount = 8;
            C_VoxelPool pool(ivec3(slotCount, 1, 1));
            pool.allocateVoxels(slotCount);
            std::vector<VoxelGpuPosition> seeded(slotCount, {vec3(-99.0f), 0.0f});
            for (std::size_t i = 0; i < slotCount; ++i) {
                pool.getPositionGlobals()[i].pos_ = vec3(static_cast<float>(i + 1));
                pool.setTransformIndexForRange(
                    i,
                    1,
                    (gpuMask & (1u << i)) ? 0u : kVoxelTransformStatic
                );
            }
            Buffer positions(
                seeded.data(),
                seeded.size() * sizeof(VoxelGpuPosition),
                BUFFER_STORAGE_DYNAMIC
            );
            for (std::size_t i = 0; i < C_VoxelPool::kMaxPendingPositionRanges; ++i) {
                pool.queuePositionRange(0, 1);
            }
            pool.queuePositionRange(slotCount - 1, 1);
            ASSERT_EQ(
                pool.getPendingPositionRanges().size(),
                C_VoxelPool::kMaxPendingPositionRanges
            );
#if defined(IR_GRAPHICS_METAL)
            if (queuedGpuWrite) {
                device_->fillBuffer(&positions, seeded.size() * sizeof(VoxelGpuPosition), 0);
            }
#endif
            IRSystem::flushPendingPositionRanges(pool, &positions);
#if defined(IR_GRAPHICS_METAL)
            device_->finish();
#endif
            std::vector<VoxelGpuPosition> result(slotCount);
            positions.getSubData(0, result.size() * sizeof(VoxelGpuPosition), result.data());
            for (std::size_t i = 0; i < slotCount; ++i) {
                const vec3 expected = (gpuMask & (1u << i))
                                          ? (queuedGpuWrite ? vec3(0.0f) : seeded[i].pos_)
                                          : pool.getPositionGlobals()[i].pos_;
                EXPECT_EQ(result[i].pos_, expected) << "slot " << i;
            }
            EXPECT_TRUE(pool.getPendingPositionRanges().empty());
        }
    }
}

#if defined(IR_GRAPHICS_METAL)
TEST_F(PositionUploadTest, ConsecutivePartialUploadsPreserveGpuWritesAndOldSnapshots) {
    using namespace IRRender;
    for (const bool reverse : {false, true}) {
        std::vector<std::uint32_t> seed(4, 0u);
        Buffer positions(seed.data(), seed.size() * sizeof(std::uint32_t), BUFFER_STORAGE_DYNAMIC);
        Buffer snapshot(seed.data(), seed.size() * sizeof(std::uint32_t), BUFFER_STORAGE_DYNAMIC);
        device_->fillBuffer(&positions, seed.size() * sizeof(std::uint32_t), 0x11);
        auto *blit = metalCommandBuffer()->blitCommandEncoder();
        blit->copyFromBuffer(
            static_cast<MTL::Buffer *>(positions.getNativeBuffer()),
            0,
            static_cast<MTL::Buffer *>(snapshot.getNativeBuffer()),
            0,
            seed.size() * sizeof(std::uint32_t)
        );
        blit->endEncoding();
        const std::uint32_t first = 0x22222222;
        const std::uint32_t second = 0x33333333;
        if (reverse) {
            positions.subData(2 * sizeof(std::uint32_t), sizeof(second), &second);
            positions.subData(sizeof(std::uint32_t), sizeof(first), &first);
        } else {
            positions.subData(sizeof(std::uint32_t), sizeof(first), &first);
            positions.subData(2 * sizeof(std::uint32_t), sizeof(second), &second);
        }
        device_->finish();
        std::vector<std::uint32_t> readback(4);
        positions.getSubData(0, readback.size() * sizeof(std::uint32_t), readback.data());
        EXPECT_EQ(readback, (std::vector<std::uint32_t>{0x11111111, first, second, 0x11111111}));
        snapshot.getSubData(0, readback.size() * sizeof(std::uint32_t), readback.data());
        EXPECT_EQ(readback, std::vector<std::uint32_t>(4, 0x11111111));
    }
}

TEST_F(PositionUploadTest, UnalignedPartialUploadPreservesPendingGpuBytes) {
    using namespace IRRender;
    for (const std::size_t byteCount : {8u, 9u}) {
        std::vector<std::uint8_t> seed(byteCount, 0x55);
        Buffer bytes(seed.data(), seed.size(), BUFFER_STORAGE_DYNAMIC);
        device_->fillBuffer(&bytes, 8, 0x11);
        const std::uint8_t patch = 0x7F;
        bytes.subData(3, 1, &patch);
        device_->finish();
        std::vector<std::uint8_t> result(byteCount);
        bytes.getSubData(0, result.size(), result.data());
        std::fill(seed.begin(), seed.begin() + 8, 0x11);
        seed[3] = patch;
        EXPECT_EQ(result, seed);
    }
}
#endif
} // namespace
#endif
