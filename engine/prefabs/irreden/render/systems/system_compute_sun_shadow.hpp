#ifndef SYSTEM_COMPUTE_SUN_SHADOW_H
#define SYSTEM_COMPUTE_SUN_SHADOW_H

// Per-pixel directional sun-shadow compute pass. For each rasterized
// surface pixel reconstructs the voxel-space surface position from the
// canvas distance texture, marches toward the sun through the 3D
// occupancy grid, and writes a brightness factor (1.0 lit, kShadowDarken
// shadowed) into the canvas sun-shadow texture consumed later by
// LIGHTING_TO_TRIXEL.
//
// Pipeline order: must run after BUILD_OCCUPANCY_GRID so the SSBO is
// fresh, after VOXEL_TO_TRIXEL_STAGE_2 / SHAPES_TO_TRIXEL so the
// distance texture is populated, and before LIGHTING_TO_TRIXEL which
// folds the shadow factor into final color.

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/math/sdf.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/cull_viewport_state.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

#include <unordered_map>
#include <vector>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

// Matches local_size_{x,y} in c_compute_sun_shadow.glsl.
constexpr int kComputeSunShadowGroupSize = 16;
constexpr int kSunShadowMarchSteps = 64;
constexpr int kMaxSunShadowShapeCasters = 8192;

namespace detail {

struct ResolvedSun {
    vec3 direction_ = IRRender::getSunDirection();
    float intensity_ = IRRender::getSunIntensity();
    float ambient_ = IRRender::getSunAmbient();
    bool shadowsEnabled_ = IRRender::getSunShadowsEnabled();
};

inline IREntity::EntityId sunShadowShapeCanvas(const C_ShapeDescriptor &shape) {
    return shape.canvasEntity_ == IREntity::kNullEntity ? IRRender::getActiveCanvasEntity()
                                                        : shape.canvasEntity_;
}

inline bool isSunShadowAnalyticShape(IRRender::ShapeType shapeType) {
    return shapeType != IRRender::ShapeType::CUSTOM_SDF;
}

inline bool
shapeCastsSunShadowAnalyticShadow(const C_ShapeDescriptor &shape, const C_LightBlocker *blocker) {
    if (!isSunShadowAnalyticShape(shape.shapeType_))
        return false;
    if ((shape.flags_ & IRRender::SHAPE_FLAG_VISIBLE) == 0u)
        return false;
    if (shape.color_.alpha_ == 0)
        return false;
    if (blocker == nullptr)
        return true;
    return blocker->castsShadow_;
}

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
            const float length = glm::length(light.direction_);
            if (length > 0.0f) {
                sun.direction_ = light.direction_ / length;
            }
            sun.intensity_ = IRMath::max(0.0f, light.intensity_);
            sun.ambient_ = IRMath::clamp(light.ambient_, 0.0f, 1.0f);
        }
    }
    sun.shadowsEnabled_ = IRRender::getSunShadowsEnabled();
    return sun;
}

inline bool isoBoundsOverlap(const IsoBounds2D &a, const IsoBounds2D &b) {
    return a.max_.x >= b.min_.x && a.min_.x <= b.max_.x && a.max_.y >= b.min_.y &&
           a.min_.y <= b.max_.y;
}

inline IsoBounds2D shadowRelevantIsoBounds(ivec2 canvasSize, vec3 sunDir) {
    constexpr int kRenderGuardMargin = 8;
    IsoBounds2D visible =
        IRRender::getCullViewport().isoViewportForCanvas(canvasSize, kRenderGuardMargin);
    const vec2 sweep = IRMath::pos3DtoPos2DIso(-sunDir * static_cast<float>(kSunShadowMarchSteps));
    visible.min_ += glm::min(sweep, vec2(0.0f));
    visible.max_ += glm::max(sweep, vec2(0.0f));
    return visible;
}

inline bool shapeCanAffectShadowView(
    const C_ShapeDescriptor &shape,
    const C_PositionGlobal3D &position,
    const IsoBounds2D &shadowRelevantBounds
) {
    auto sdfType = static_cast<IRMath::SDF::ShapeType>(shape.shapeType_);
    const float radius = IRMath::SDF::boundingRadius(sdfType, shape.params_);
    const vec2 isoCenter = IRMath::pos3DtoPos2DIso(position.pos_);
    const vec2 isoHalfExtent = IRMath::shapeIsoHalfExtent(vec3(radius * 2.0f));
    return isoBoundsOverlap(
        IsoBounds2D{isoCenter - isoHalfExtent, isoCenter + isoHalfExtent},
        shadowRelevantBounds
    );
}

inline void collectShapeCastersForCanvas(
    IREntity::EntityId canvasEntity,
    ivec2 canvasSize,
    vec3 sunDir,
    std::vector<GPUShapeDescriptor> &casters
) {
    casters.clear();
    const IsoBounds2D shadowRelevantBounds = shadowRelevantIsoBounds(canvasSize, sunDir);

    const auto include = IREntity::getArchetype<C_ShapeDescriptor, C_PositionGlobal3D>();
    const IREntity::ComponentId blockerType = IREntity::getComponentType<C_LightBlocker>();
    auto nodes = IREntity::queryArchetypeNodesSimple(include);

    for (auto *node : nodes) {
        auto &shapes = IREntity::getComponentData<C_ShapeDescriptor>(node);
        auto &positions = IREntity::getComponentData<C_PositionGlobal3D>(node);
        const bool hasBlocker = node->type_.contains(blockerType);
        std::vector<C_LightBlocker> *blockers = nullptr;
        if (hasBlocker) {
            blockers = &IREntity::getComponentData<C_LightBlocker>(node);
        }

        for (int i = 0; i < node->length_; ++i) {
            const C_ShapeDescriptor &shape = shapes[i];
            const C_LightBlocker *blocker = blockers != nullptr ? &(*blockers)[i] : nullptr;
            if (!shapeCastsSunShadowAnalyticShadow(shape, blocker))
                continue;
            if (sunShadowShapeCanvas(shape) != canvasEntity)
                continue;
            if (!shapeCanAffectShadowView(shape, positions[i], shadowRelevantBounds))
                continue;

            IR_ASSERT(
                static_cast<int>(casters.size()) < kMaxSunShadowShapeCasters,
                "Too many analytic sun-shadow casters for one canvas"
            );
            if (static_cast<int>(casters.size()) >= kMaxSunShadowShapeCasters)
                continue;

            GPUShapeDescriptor caster{};
            caster.worldPosition = vec4(positions[i].pos_, 1.0f);
            caster.params = shape.params_;
            caster.shapeType = static_cast<std::uint32_t>(shape.shapeType_);
            caster.color = shape.color_.toPackedRGBA();
            caster.entityId = node->entities_[i];
            caster.flags = shape.flags_;
            caster.lodLevel = shape.lodLevel_;
            casters.push_back(caster);
        }
    }
}

} // namespace detail

template <> struct System<COMPUTE_SUN_SHADOW> {
    static SystemId create() {
        static FrameDataSun frameData{};
        using CanvasId = IREntity::EntityId;
        static std::unordered_map<CanvasId, std::vector<GPUShapeDescriptor>> shapeCastersByCanvas;

        IRRender::createNamedResource<ShaderProgram>(
            "ComputeSunShadowProgram",
            std::vector{ShaderStage{IRRender::kFileCompComputeSunShadow, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<Buffer>(
            "ComputeSunShadowFrameData",
            nullptr,
            sizeof(FrameDataSun),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_FrameDataSun
        );
        IRRender::createNamedResource<Buffer>(
            "SunShadowShapeCasterBuffer",
            nullptr,
            kMaxSunShadowShapeCasters * sizeof(GPUShapeDescriptor),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_SunShadowShapeCasters
        );

        static ShaderProgram *s_program =
            IRRender::getNamedResource<ShaderProgram>("ComputeSunShadowProgram");
        static Buffer *s_sunShadowFrameDataBuf =
            IRRender::getNamedResource<Buffer>("ComputeSunShadowFrameData");
        static Buffer *s_occupancySSBO = IRRender::getNamedResource<Buffer>("OccupancyGridBuffer");
        static Buffer *s_voxelFrameDataBuf =
            IRRender::getNamedResource<Buffer>("SingleVoxelFrameData");
        static Buffer *s_shapeCasterSSBO =
            IRRender::getNamedResource<Buffer>("SunShadowShapeCasterBuffer");

        return createSystem<
            C_TriangleCanvasTextures,
            C_CanvasSunShadow,
            C_TrixelCanvasRenderBehavior>(
            "ComputeSunShadow",
            [](IREntity::EntityId &canvasEntity,
               const C_TriangleCanvasTextures &canvasTextures,
               const C_CanvasSunShadow &shadow,
               const C_TrixelCanvasRenderBehavior &behavior) {
                // Skip GUI-only canvases — same rationale as the AO pass.
                if (!behavior.useCameraPositionIso_)
                    return;
                IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

                auto &shapeCasters = shapeCastersByCanvas[canvasEntity];
                {
                    IR_PROFILE_BLOCK("ComputeSunShadow::CollectCasters", IR_PROFILER_COLOR_RENDER);
                    detail::collectShapeCastersForCanvas(
                        canvasEntity,
                        canvasTextures.size_,
                        vec3(frameData.sunDirection_),
                        shapeCasters
                    );
                }
                frameData.shapeCasterCount_ = static_cast<int>(shapeCasters.size());

                auto &timing = IRRender::gpuStageTiming();
                IRRender::TimePoint t0;
                if (timing.enabled_) {
                    IRRender::device()->finish();
                    t0 = IRRender::SteadyClock::now();
                }

                canvasTextures.getTextureDistances()
                    ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
                shadow.getTexture()
                    ->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
                canvasTextures.getTextureEntityIds()
                    ->bindAsImage(2, TextureAccess::READ_ONLY, TextureFormat::RG32UI);
                s_occupancySSBO->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_OccupancyGrid);
                s_shapeCasterSSBO->bindBase(
                    BufferTarget::SHADER_STORAGE,
                    kBufferIndex_SunShadowShapeCasters
                );
                s_voxelFrameDataBuf->bindBase(
                    BufferTarget::UNIFORM,
                    kBufferIndex_FrameDataVoxelToCanvas
                );
                s_sunShadowFrameDataBuf->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataSun);

                if (!shapeCasters.empty()) {
                    s_shapeCasterSSBO->subData(
                        0,
                        shapeCasters.size() * sizeof(GPUShapeDescriptor),
                        shapeCasters.data()
                    );
                }
                s_sunShadowFrameDataBuf->subData(0, sizeof(FrameDataSun), &frameData);

                const int groupsX =
                    IRMath::divCeil(canvasTextures.size_.x, kComputeSunShadowGroupSize);
                const int groupsY =
                    IRMath::divCeil(canvasTextures.size_.y, kComputeSunShadowGroupSize);
                IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);

                // Assumes a single matching canvas per frame. Switch to `+=`
                // with a `beginTick` reset if the filter ever matches
                // multiple entities — otherwise later entities overwrite.
                if (timing.enabled_) {
                    IRRender::device()->finish();
                    timing.computeSunShadowMs_ =
                        IRRender::elapsedMs(t0, IRRender::SteadyClock::now());
                }
            },
            []() {
                s_program->use();
                for (auto &[_, casters] : shapeCastersByCanvas) {
                    casters.clear();
                }
                const detail::ResolvedSun sun = detail::resolveSun();
                frameData.sunDirection_ = vec4(sun.direction_, 0.0f);
                frameData.sunIntensity_ = sun.intensity_;
                frameData.sunAmbient_ = sun.ambient_;
                frameData.shadowsEnabled_ = sun.shadowsEnabled_ ? 1 : 0;
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_COMPUTE_SUN_SHADOW_H */
