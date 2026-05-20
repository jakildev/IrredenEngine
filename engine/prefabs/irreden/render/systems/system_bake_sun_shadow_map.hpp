#ifndef SYSTEM_BAKE_SUN_SHADOW_MAP_H
#define SYSTEM_BAKE_SUN_SHADOW_MAP_H

// Bakes a sun-aligned 2D depth buffer from the iso canvas distance
// texture so COMPUTE_SUN_SHADOW resolves shadows with a single texel
// read per pixel.
//
// Pipeline: must run after the geometry stages (trixelDistances must be
// populated) and before COMPUTE_SUN_SHADOW (which reads the baked buffer).
// Owns the full FrameDataSun UBO upload — sun direction, basis, AABB,
// flags — so downstream consumers (COMPUTE_SUN_SHADOW, COMPUTE_VOXEL_AO,
// LIGHTING_TO_TRIXEL) read coherent state every frame.
//
// Slot 28 is shared with the occupancy grid (read by AO + light-volume).
// Both producers rebind the slot before their own dispatch, so the alias
// is safe.

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/cull_viewport_state.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>
#include <irreden/render/sun_shadow_constants.hpp>

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
// `kSunShadowMaxDistance` (the AABB-sweep length, also consumed by the iso
// rasterizers' shadow-feeder cull) lives in `sun_shadow_constants.hpp` so
// both producers stay in lockstep.
using IRPrefab::SunShadow::kSunShadowMaxDistance;
constexpr int kBakeSunShadowGroupSize = 16;
constexpr int kIsoFrustumCornerCount = 8;

namespace detail {

struct ResolvedSun {
    vec3 direction_ = IRRender::getSunDirection();
    float intensity_ = IRRender::getSunIntensity();
    float ambient_ = IRRender::getSunAmbient();
    bool shadowsEnabled_ = IRRender::getSunShadowsEnabled();
    bool aoEnabled_ = IRRender::getAOEnabled();
};

// A single ECS entity carrying `C_LightSource{type=DIRECTIONAL}` overrides
// the global sun. The first three fields snapshot RenderManager state, so
// scenes without a directional light entity still get coherent values.
inline ResolvedSun resolveSun() {
    ResolvedSun sun{};
    const auto include = IREntity::getArchetype<C_LightSource>();
    auto nodes = IREntity::queryArchetypeNodesSimple(include);
    bool foundDirectional = false;
    for (auto *node : nodes) {
        auto &lights = IREntity::getComponentData<C_LightSource>(node);
        for (int i = 0; i < node->length_; ++i) {
            const C_LightSource &light = lights[i];
            if (light.type_ != LightType::DIRECTIONAL)
                continue;
            IR_ASSERT(!foundDirectional, "Only one DIRECTIONAL light may drive the global sun");
            foundDirectional = true;

            IR_ASSERT(
                light.direction_.z <= 0.0f,
                "Directional light points from the world toward the sun; +Z is down, so z must be "
                "<= 0"
            );
            const float length = IRMath::length(light.direction_);
            if (length > 0.0f) {
                sun.direction_ = light.direction_ / length;
            }
            sun.intensity_ = IRMath::max(0.0f, light.intensity_);
            sun.ambient_ = IRMath::clamp(light.ambient_, 0.0f, 1.0f);
        }
    }
    sun.shadowsEnabled_ = IRRender::getSunShadowsEnabled();
    sun.aoEnabled_ = IRRender::getAOEnabled();
    return sun;
}

} // namespace detail

template <> struct System<BAKE_SUN_SHADOW_MAP> {
    ShaderProgram *clearProgram_ = nullptr;
    ShaderProgram *bakeProgram_ = nullptr;
    Buffer *sunShadowDepthMap_ = nullptr;
    // ComputeSunShadowFrameData is created by COMPUTE_SUN_SHADOW,
    // which is constructed AFTER BAKE in pipeline registration
    // order. Resolved lazily on the first beginTick (which fires
    // before the per-entity tick).
    Buffer *sunShadowFrameDataBuf_ = nullptr;
    Buffer *voxelFrameDataBuf_ = nullptr;
    FrameDataSun frameData_{};

    void tick(
        const C_TriangleCanvasTextures &canvasTextures,
        const C_CanvasSunShadow &,
        const C_TrixelCanvasRenderBehavior &behavior
    ) {
        if (!behavior.useCameraPositionIso_) {
            return;
        }

        // BAKE owns the FrameDataSun UBO: upload the full struct
        // each frame so all downstream consumers see fresh state.
        sunShadowFrameDataBuf_->subData(0, sizeof(FrameDataSun), &frameData_);
        if (frameData_.shadowsEnabled_ == 0) {
            return;
        }

        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

        // Pass 1: clear the sun depth map to 0xFFFFFFFF.
        clearProgram_->use();
        sunShadowDepthMap_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_SunShadowDepthMap);
        const int clearGroups = IRMath::divCeil(kSunShadowMapDim, kBakeSunShadowGroupSize);
        IRRender::device()->dispatchCompute(clearGroups, clearGroups, 1);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);

        // Pass 2: project each iso-canvas surface pixel into the
        // sun depth map via atomicMin.
        bakeProgram_->use();
        canvasTextures.getTextureDistances()
            ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
        sunShadowDepthMap_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_SunShadowDepthMap);
        voxelFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataVoxelToCanvas);
        sunShadowFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataSun);
        const int groupsX = IRMath::divCeil(canvasTextures.size_.x, kBakeSunShadowGroupSize);
        const int groupsY = IRMath::divCeil(canvasTextures.size_.y, kBakeSunShadowGroupSize);
        IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
    }

    void beginTick() {
        if (sunShadowFrameDataBuf_ == nullptr) {
            sunShadowFrameDataBuf_ =
                IRRender::getNamedResource<Buffer>("ComputeSunShadowFrameData");
        }

        const detail::ResolvedSun sun = detail::resolveSun();
        vec3 sunDir = sun.direction_;
        const float sunLen = IRMath::length(sunDir);
        if (sunLen > 0.0f) {
            sunDir /= sunLen;
        }
        frameData_.sunDirection_ = vec4(sunDir, 0.0f);
        frameData_.sunIntensity_ = sun.intensity_;
        frameData_.sunAmbient_ = sun.ambient_;
        frameData_.shadowsEnabled_ = sun.shadowsEnabled_ ? 1 : 0;
        frameData_.aoEnabled_ = sun.aoEnabled_ ? 1 : 0;
        if (frameData_.shadowsEnabled_ == 0) {
            return;
        }

        // Pick a reference axis far from sunDir for numerical
        // stability under near-vertical suns.
        const vec3 ref =
            (IRMath::abs(sunDir.z) < 0.9f) ? vec3(0.0f, 0.0f, 1.0f) : vec3(1.0f, 0.0f, 0.0f);
        vec3 uHat = IRMath::cross(ref, sunDir);
        const float uLen = IRMath::length(uHat);
        if (uLen > 0.0f) {
            uHat /= uLen;
        }
        vec3 vHat = IRMath::cross(sunDir, uHat);
        frameData_.sunBasisU_ = vec4(uHat, 0.0f);
        frameData_.sunBasisV_ = vec4(vHat, 0.0f);

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
        frameData_.sunBufferOriginUV_ = sunUVMin;
        frameData_.sunBufferTexelSize_ = vec2(texelSize, texelSize);
    }

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

        SystemId systemId = registerSystem<
            BAKE_SUN_SHADOW_MAP,
            C_TriangleCanvasTextures,
            C_CanvasSunShadow,
            C_TrixelCanvasRenderBehavior>("BakeSunShadowMap");
        auto *p = getSystemParams<System<BAKE_SUN_SHADOW_MAP>>(systemId);
        p->clearProgram_ = IRRender::getNamedResource<ShaderProgram>("ClearSunShadowMapProgram");
        p->bakeProgram_ = IRRender::getNamedResource<ShaderProgram>("BakeSunShadowMapProgram");
        p->sunShadowDepthMap_ = IRRender::getNamedResource<Buffer>("SunShadowDepthMap");
        p->voxelFrameDataBuf_ = IRRender::getNamedResource<Buffer>("SingleVoxelFrameData");
        IRRender::tagGpuStage(systemId, "bakeSunShadowMap");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_BAKE_SUN_SHADOW_MAP_H */
