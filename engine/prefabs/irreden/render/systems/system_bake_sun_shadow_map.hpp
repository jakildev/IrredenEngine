#ifndef SYSTEM_BAKE_SUN_SHADOW_MAP_H
#define SYSTEM_BAKE_SUN_SHADOW_MAP_H

// Bakes a sun-aligned 2D depth buffer from the iso canvas distance
// texture so COMPUTE_SUN_SHADOW can replace its 64-step march with a
// single texel read. Flag-guarded by IRRender::setScreenSpaceShadowsEnabled.
//
// Pipeline: must run after the geometry stages (trixelDistances must be
// populated) and before COMPUTE_SUN_SHADOW (which reads the baked buffer).
//
// Slot 28 is shared with the legacy occupancy grid; both systems rebind
// it before their own dispatch.

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/cull_viewport_state.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

// Must match `kSunShadowMapDim` in c_clear_sun_shadow_map.glsl /
// c_bake_sun_shadow_map.glsl (and Metal counterparts).
constexpr int kSunShadowMapDim = 1024;
// Sweeps the visible AABB along -sunDir before projecting to sun-space,
// so off-screen surfaces within this many voxels still cast into-frame
// shadows. Mirrors the legacy march length so shadow distance survives
// the flag flip.
constexpr float kSunShadowMaxDistance = 64.0f;
constexpr int kBakeSunShadowGroupSize = 16;
constexpr int kIsoFrustumCornerCount = 8;

template <> struct System<BAKE_SUN_SHADOW_MAP> {
    struct Params {
        ShaderProgram *clearProgram_ = nullptr;
        ShaderProgram *bakeProgram_ = nullptr;
        Buffer *sunShadowDepthMap_ = nullptr;
        Buffer *sunShadowFrameDataBuf_ = nullptr;
        Buffer *voxelFrameDataBuf_ = nullptr;
        // Holds the BAKE-owned slice (offset useScreenSpaceShadow_ onward).
        // Uploaded as a partial subData so the legacy prefix written by
        // COMPUTE_SUN_SHADOW survives.
        FrameDataSun frameData_{};
    };

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "ClearSunShadowMapProgram",
            std::vector{ShaderStage{IRRender::kFileCompClearSunShadowMap, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<ShaderProgram>(
            "BakeSunShadowMapProgram",
            std::vector{ShaderStage{IRRender::kFileCompBakeSunShadowMap, ShaderType::COMPUTE}}
        );
        // Sun-space depth buffer. uint per texel; clear pass resets to
        // 0xFFFFFFFF (lit-sentinel) before each bake.
        IRRender::createNamedResource<Buffer>(
            "SunShadowDepthMap",
            nullptr,
            static_cast<std::size_t>(kSunShadowMapDim) *
                static_cast<std::size_t>(kSunShadowMapDim) * sizeof(std::uint32_t),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_SunShadowDepthMap
        );

        auto paramsOwner = std::make_unique<Params>();
        Params *p = paramsOwner.get();
        p->clearProgram_ = IRRender::getNamedResource<ShaderProgram>("ClearSunShadowMapProgram");
        p->bakeProgram_ = IRRender::getNamedResource<ShaderProgram>("BakeSunShadowMapProgram");
        p->sunShadowDepthMap_ = IRRender::getNamedResource<Buffer>("SunShadowDepthMap");
        // ComputeSunShadowFrameData is created by COMPUTE_SUN_SHADOW,
        // which is constructed AFTER BAKE in pipeline registration
        // order. Resolved lazily on the first beginTick (which fires
        // before the per-entity tick).
        p->voxelFrameDataBuf_ = IRRender::getNamedResource<Buffer>("SingleVoxelFrameData");

        SystemId systemId =
            createSystem<C_TriangleCanvasTextures, C_CanvasSunShadow, C_TrixelCanvasRenderBehavior>(
                "BakeSunShadowMap",
                [p](const C_TriangleCanvasTextures &canvasTextures,
                    const C_CanvasSunShadow &,
                    const C_TrixelCanvasRenderBehavior &behavior) {
                    if (!behavior.useCameraPositionIso_) {
                        return;
                    }

                    // BAKE-owned region: when active, upload flag + basis +
                    // AABB; when gated off, upload just the flag (+ padding)
                    // so COMPUTE sees the live toggle without reading stale
                    // basis. COMPUTE overwrites the legacy prefix later this
                    // frame — non-overlapping byte ranges.
                    constexpr std::size_t kBakeFieldsOffset =
                        offsetof(FrameDataSun, useScreenSpaceShadow_);
                    constexpr std::size_t kBasisOffset =
                        offsetof(FrameDataSun, sunBasisU_);
                    constexpr std::size_t kFlagFieldsSize =
                        kBasisOffset - kBakeFieldsOffset;
                    constexpr std::size_t kBakeFieldsSize =
                        sizeof(FrameDataSun) - kBakeFieldsOffset;

                    const bool bakeActive =
                        p->frameData_.useScreenSpaceShadow_ != 0 &&
                        p->frameData_.shadowsEnabled_ != 0;
                    const std::size_t uploadSize =
                        bakeActive ? kBakeFieldsSize : kFlagFieldsSize;
                    const auto *base = reinterpret_cast<const std::byte *>(&p->frameData_);
                    p->sunShadowFrameDataBuf_
                        ->subData(kBakeFieldsOffset, uploadSize, base + kBakeFieldsOffset);
                    if (!bakeActive) {
                        return;
                    }

                    IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

                    // Pass 1: clear the sun depth map to 0xFFFFFFFF.
                    p->clearProgram_->use();
                    p->sunShadowDepthMap_->bindBase(
                        BufferTarget::SHADER_STORAGE,
                        kBufferIndex_SunShadowDepthMap
                    );
                    const int clearGroups =
                        IRMath::divCeil(kSunShadowMapDim, kBakeSunShadowGroupSize);
                    IRRender::device()->dispatchCompute(clearGroups, clearGroups, 1);
                    IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);

                    // Pass 2: project each iso-canvas surface pixel into the
                    // sun depth map via atomicMin.
                    p->bakeProgram_->use();
                    canvasTextures.getTextureDistances()
                        ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
                    p->sunShadowDepthMap_->bindBase(
                        BufferTarget::SHADER_STORAGE,
                        kBufferIndex_SunShadowDepthMap
                    );
                    p->voxelFrameDataBuf_->bindBase(
                        BufferTarget::UNIFORM,
                        kBufferIndex_FrameDataVoxelToCanvas
                    );
                    p->sunShadowFrameDataBuf_->bindBase(
                        BufferTarget::UNIFORM,
                        kBufferIndex_FrameDataSun
                    );
                    const int groupsX =
                        IRMath::divCeil(canvasTextures.size_.x, kBakeSunShadowGroupSize);
                    const int groupsY =
                        IRMath::divCeil(canvasTextures.size_.y, kBakeSunShadowGroupSize);
                    IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
                    IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
                },
                [p]() {
                    if (p->sunShadowFrameDataBuf_ == nullptr) {
                        p->sunShadowFrameDataBuf_ =
                            IRRender::getNamedResource<Buffer>("ComputeSunShadowFrameData");
                    }
                    p->frameData_.useScreenSpaceShadow_ =
                        IRRender::getScreenSpaceShadowsEnabled() ? 1 : 0;
                    p->frameData_.shadowsEnabled_ = IRRender::getSunShadowsEnabled() ? 1 : 0;
                    if (p->frameData_.useScreenSpaceShadow_ == 0 ||
                        p->frameData_.shadowsEnabled_ == 0) {
                        return;
                    }

                    // Read sunDir directly from RenderManager rather than
                    // resolveSun() — when an ECS directional light overrides
                    // it, the lookup picks up the override one frame later;
                    // baking last frame's direction is benign for static suns.
                    vec3 sunDir = IRRender::getSunDirection();
                    const float sunLen = IRMath::length(sunDir);
                    if (sunLen > 0.0f) {
                        sunDir /= sunLen;
                    }
                    p->frameData_.sunDirection_ = vec4(sunDir, 0.0f);

                    // Pick a reference axis far from sunDir for numerical
                    // stability under near-vertical suns.
                    const vec3 ref = (std::abs(sunDir.z) < 0.9f) ? vec3(0.0f, 0.0f, 1.0f)
                                                                 : vec3(1.0f, 0.0f, 0.0f);
                    vec3 uHat = IRMath::cross(ref, sunDir);
                    const float uLen = IRMath::length(uHat);
                    if (uLen > 0.0f) {
                        uHat /= uLen;
                    }
                    vec3 vHat = IRMath::cross(sunDir, uHat);
                    p->frameData_.sunBasisU_ = vec4(uHat, 0.0f);
                    p->frameData_.sunBasisV_ = vec4(vHat, 0.0f);

                    const auto &cull = IRRender::getCullViewport();
                    const ivec2 canvasSize = cull.canvasSize_;
                    const IsoBounds2D isoBounds = cull.isoViewportForCanvas(canvasSize, 0);

                    // Iso depth axis is (1,1,1). Range chosen to cover the
                    // common demo extent; oversize is wasted texels but never
                    // truncates a shadow.
                    constexpr float kIsoDepthMin = -256.0f;
                    constexpr float kIsoDepthMax = 256.0f;

                    std::array<vec3, kIsoFrustumCornerCount> corners{};
                    int idx = 0;
                    for (float depth : {kIsoDepthMin, kIsoDepthMax}) {
                        for (int y : {isoBounds.min_.y, isoBounds.max_.y}) {
                            for (int x : {isoBounds.min_.x, isoBounds.max_.x}) {
                                corners[idx++] = IRMath::isoPixelToPos3D(x, y, depth);
                            }
                        }
                    }

                    // Sweep along -sunDir for shadow-feeder coverage.
                    const vec3 sweep = -sunDir * kSunShadowMaxDistance;
                    vec2 sunUVMin = vec2(std::numeric_limits<float>::max());
                    vec2 sunUVMax = vec2(std::numeric_limits<float>::lowest());
                    for (const vec3 &c : corners) {
                        for (const vec3 &offset : {vec3(0.0f), sweep}) {
                            const vec3 p3 = c + offset;
                            const vec2 sunUV = vec2(IRMath::dot(p3, uHat), IRMath::dot(p3, vHat));
                            sunUVMin = IRMath::min(sunUVMin, sunUV);
                            sunUVMax = IRMath::max(sunUVMax, sunUV);
                        }
                    }

                    // Pad so corners don't quantize off-buffer; the larger AABB
                    // dim sets texelSize, the other gets unused margin.
                    constexpr float kAABBPad = 1.0f;
                    sunUVMin -= vec2(kAABBPad);
                    sunUVMax += vec2(kAABBPad);
                    const vec2 extent = sunUVMax - sunUVMin;
                    const float maxExtent = IRMath::max(extent.x, extent.y);
                    const float texelSize = maxExtent / static_cast<float>(kSunShadowMapDim);
                    p->frameData_.sunBufferOriginUV_ = sunUVMin;
                    p->frameData_.sunBufferTexelSize_ = vec2(texelSize, texelSize);
                }
            );

        setSystemParams(systemId, std::move(paramsOwner));
        IRRender::tagGpuStage(systemId, "bakeSunShadowMap");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_BAKE_SUN_SHADOW_MAP_H */
