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
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/camera.hpp>
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
constexpr int kSunShadowCascadeCount = 2;
constexpr float kCascadeSplitRatio = 0.4f;

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
    Buffer *sunShadowFrameDataBuf_ = nullptr;
    Buffer *voxelFrameDataBuf_ = nullptr;
    FrameDataSun frameData_{};

    // Smooth camera Z-yaw (#1311): main canvas + per-axis voxel canvases,
    // re-resolved every frame in beginTick. Null unless allocated (rotating).
    IREntity::EntityId perAxisCanvasEntity_ = IREntity::kNullEntity;
    C_PerAxisTrixelCanvases *perAxisCanvases_ = nullptr;

    void tick(
        IREntity::EntityId entity,
        const C_TriangleCanvasTextures &canvasTextures,
        const C_CanvasSunShadow &,
        const C_TrixelCanvasRenderBehavior &behavior
    ) {
        if (!behavior.useCameraPositionIso_) {
            return;
        }

        sunShadowFrameDataBuf_->subData(0, sizeof(FrameDataSun), &frameData_);
        if (frameData_.shadowsEnabled_ == 0) {
            return;
        }

        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

        // Clear the full depth map (both cascades) to 0xFFFFFFFF.
        clearProgram_->use();
        sunShadowDepthMap_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_SunShadowDepthMap);
        const int totalClearDim = kSunShadowMapDim;
        const int clearGroupsX = IRMath::divCeil(totalClearDim, kBakeSunShadowGroupSize);
        // Y covers kSunShadowMapDim * kSunShadowCascadeCount rows — cascades
        // are stacked vertically in the 1024×2048 linearised buffer.
        const int clearGroupsY =
            IRMath::divCeil(totalClearDim * kSunShadowCascadeCount, kBakeSunShadowGroupSize);
        IRRender::device()->dispatchCompute(clearGroupsX, clearGroupsY, 1);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);

        // Bake: each pixel projects into both cascades in one pass.
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

        // Smooth camera Z-yaw (#1311): bake the three per-axis voxel canvases
        // into the SAME shared depth map (no extra clear) so the smooth voxels
        // and the main canvas's SDF/text shadow each other under rotation
        // (architect direction, option (b) — bake pre-composite from the trixel
        // maps, not from a resolved framebuffer projection).
        if (entity == perAxisCanvasEntity_ && perAxisCanvases_ != nullptr &&
            perAxisCanvases_->isAllocated()) {
            dispatchPerAxisBake(*perAxisCanvases_, canvasTextures);
        }
    }

    // Bake each per-axis voxel canvas into the shared sun depth map. The bake
    // program, SSBO (binding 28), and FrameDataSun stay bound; only the input
    // distance image and the perAxisRoute selector change. Restores
    // perAxisRoute to 0 so COMPUTE_SUN_SHADOW's main-canvas pass is unaffected.
    void dispatchPerAxisBake(
        C_PerAxisTrixelCanvases &axes, const C_TriangleCanvasTextures &mainTextures
    ) {
        // perAxisRoute is a boolean route flag on the lighting path (any nonzero
        // = per-axis canvas); the shader recovers the axis per-pixel from faceId,
        // NOT from this field — distinct from stage-1's 1/2/3 = X/Y/Z axis selector.
        const int kPerAxisRoute = 1;
        voxelFrameDataBuf_
            ->subData(offsetof(FrameDataVoxelToCanvas, perAxisRoute_), sizeof(int), &kPerAxisRoute);
        const int groupsX = IRMath::divCeil(axes.size_.x, kBakeSunShadowGroupSize);
        const int groupsY = IRMath::divCeil(axes.size_.y, kBakeSunShadowGroupSize);
        for (int axis = 0; axis < C_PerAxisTrixelCanvases::kAxisCount; ++axis) {
            axes.axes_[axis]
                .distances_.second->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
            IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
        }
        // One barrier after the 3 independent per-axis dispatches
        // (separate textures; the bake's atomicMin is order-safe) so they
        // overlap on the GPU instead of serializing per axis (#1311).
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
        const int kSingleCanvasRoute = 0;
        voxelFrameDataBuf_->subData(
            offsetof(FrameDataVoxelToCanvas, perAxisRoute_),
            sizeof(int),
            &kSingleCanvasRoute
        );
        // Restore the main-canvas distance image binding (see the #1311 note in
        // system_compute_voxel_ao.hpp — the persistent Metal image-binding table
        // would otherwise dangle when release() frees the per-axis textures).
        mainTextures.getTextureDistances()
            ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
    }

    void beginTick() {
        if (sunShadowFrameDataBuf_ == nullptr) {
            sunShadowFrameDataBuf_ =
                IRRender::getNamedResource<Buffer>("ComputeSunShadowFrameData");
        }

        // Resolve the main canvas + its per-axis voxel canvases (#1311). Re-
        // resolved every frame; null unless the component exists AND is allocated.
        perAxisCanvasEntity_ = IRRender::getCanvas("main");
        perAxisCanvases_ = nullptr;
        if (perAxisCanvasEntity_ != IREntity::kNullEntity) {
            auto perAxis =
                IREntity::getComponentOptional<C_PerAxisTrixelCanvases>(perAxisCanvasEntity_);
            if (perAxis.has_value()) {
                perAxisCanvases_ = perAxis.value();
            }
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

        vec3 uHat, vHat;
        IRMath::buildOrthonormalBasis(sunDir, uHat, vHat);
        frameData_.sunBasisU_ = vec4(uHat, 0.0f);
        frameData_.sunBasisV_ = vec4(vHat, 0.0f);

        const auto &cull = IRRender::getCullViewport();
        const ivec2 canvasSize = cull.canvasSize_;
        const IsoBounds2D isoBounds = cull.isoViewportForCanvas(canvasSize, 0);

        constexpr float kIsoDepthMin = -256.0f;
        constexpr float kIsoDepthMax = 256.0f;
        const float splitDepth = kIsoDepthMin + (kIsoDepthMax - kIsoDepthMin) * kCascadeSplitRatio;

        struct CascadeDepthRange {
            float min_;
            float max_;
        };
        const CascadeDepthRange cascadeRanges[kSunShadowCascadeCount] = {
            {kIsoDepthMin, splitDepth},
            {kIsoDepthMin, kIsoDepthMax},
        };

        const vec3 sweep = -sunDir * kSunShadowMaxDistance;
        constexpr float kAABBPad = 1.0f;
        const float dimF = static_cast<float>(kSunShadowMapDim);

        // isoPixelToPos3D returns corners in the rasterYaw-rotated canvas
        // frame. The sun-direction sweep below is in world frame. Lift corners
        // into world frame once, before the per-cascade AABB sweep, so both
        // sides share the same coordinate frame. No-op at rasterYaw == 0.
        const auto cardinalIndex = IRMath::rasterYawCardinalIndex(IRPrefab::Camera::getRasterYaw());

        for (int ci = 0; ci < kSunShadowCascadeCount; ++ci) {
            std::array<vec3, kIsoFrustumCornerCount> corners{};
            int idx = 0;
            for (float depth : {cascadeRanges[ci].min_, cascadeRanges[ci].max_}) {
                for (int y : {isoBounds.min_.y, isoBounds.max_.y}) {
                    for (int x : {isoBounds.min_.x, isoBounds.max_.x}) {
                        corners[idx++] = IRMath::isoPixelToPos3D(x, y, depth);
                    }
                }
            }
            for (auto &c : corners) {
                c = IRMath::rotateCardinalZInv(c, cardinalIndex);
            }

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

            sunUVMin -= vec2(kAABBPad);
            sunUVMax += vec2(kAABBPad);
            vec2 extent = sunUVMax - sunUVMin;
            vec2 texelSize = vec2(extent.x / dimF, extent.y / dimF);

            // Snap both edges to the texel grid so bake and lookup share the
            // same phase; recompute texelSize so the buffer covers the full
            // snapped extent symmetrically (asymmetric snap eats high-side pad
            // at texelSize > kAABBPad on wide viewports).
            sunUVMin.x = IRMath::floor(sunUVMin.x / texelSize.x) * texelSize.x;
            sunUVMin.y = IRMath::floor(sunUVMin.y / texelSize.y) * texelSize.y;
            sunUVMax.x = IRMath::ceil(sunUVMax.x / texelSize.x) * texelSize.x;
            sunUVMax.y = IRMath::ceil(sunUVMax.y / texelSize.y) * texelSize.y;
            texelSize = vec2((sunUVMax.x - sunUVMin.x) / dimF, (sunUVMax.y - sunUVMin.y) / dimF);

            if (ci == 0) {
                frameData_.cascadeOriginUV_0_ = sunUVMin;
                frameData_.cascadeTexelSize_0_ = texelSize;
                frameData_.sunBufferOriginUV_ = sunUVMin;
                frameData_.sunBufferTexelSize_ = texelSize;
            } else {
                frameData_.cascadeOriginUV_1_ = sunUVMin;
                frameData_.cascadeTexelSize_1_ = texelSize;
            }
        }
        frameData_.cascadeSplitDepth_ = splitDepth;
        frameData_.cascadeCount_ = kSunShadowCascadeCount;
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
        IRRender::createNamedResource<Buffer>(
            "SunShadowDepthMap",
            nullptr,
            static_cast<std::size_t>(kSunShadowMapDim) *
                static_cast<std::size_t>(kSunShadowMapDim) * kSunShadowCascadeCount *
                sizeof(std::uint32_t),
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
