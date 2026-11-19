#ifndef SYSTEM_COMPUTE_SUN_SHADOW_H
#define SYSTEM_COMPUTE_SUN_SHADOW_H

// Per-pixel directional sun-shadow compute pass. For each rasterized
// surface pixel reconstructs the voxel-space surface position from the
// canvas distance texture, samples the sun-aligned depth map baked by
// BAKE_SUN_SHADOW_MAP, and writes a brightness factor (1.0 lit,
// kShadowDarken shadowed) into the canvas sun-shadow texture consumed
// later by LIGHTING_TO_TRIXEL.
//
// Pipeline order: must run after BAKE_SUN_SHADOW_MAP (writes the depth
// map this pass reads) and after VOXEL_TO_TRIXEL_STAGE_1 / SHAPES_TO_TRIXEL
// (so the distance texture is populated), and before LIGHTING_TO_TRIXEL
// which folds the shadow factor into final color.
//
// Unwritten texels. The single-canvas grid dispatch stores nothing into an
// empty texel, nor (surface variant) into a cardinal voxel surface. On a
// cardinal surface-lit frame the main canvas's whole dispatch is skipped when
// C_TriangleCanvasTextures::holdsQueriedContent() says every occupied texel is
// a cardinal voxel surface. Those texels keep whatever an earlier frame or
// route stored. That is safe only because every reader skips them on the frame
// this pass did:
//   - LIGHTING_TO_TRIXEL returns on the empty sentinel, and skips cardinal
//     voxel surfaces under this frame's resolveRoute (cardinal_ and not
//     cardinalGlitch_, zero residual yaw). IR_ASSERTs on both sides check that
//     the two skip sets agree.
//   - C_CanvasSunShadow::readFactorsSynced has one caller, the fog demo's
//     ceiling probe, which reads it only on a frame without a surface route.
//     On a surface route the surface variant stores nothing for a cardinal
//     voxel surface, fog canvas or not, and the probe reads the composited
//     frame instead.
// Ordering precondition: every distance and entity-id writer of a canvas runs
// before this pass. A writer moved between it and LIGHTING_TO_TRIXEL, a new
// reader of the canvas sun-shadow texture, or a non-detached non-main canvas
// sampling the main canvas's placeholder would read stale values.

#include <irreden/ir_render.hpp>
#include <irreden/render/shape_receiver_bindings.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/per_axis_canvas.hpp>
#include <irreden/render/surface_lighting_route.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

#include <irreden/render/camera.hpp>

#include <cstddef>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

// Matches local_size_{x,y} in c_compute_sun_shadow.glsl.
constexpr int kComputeSunShadowGroupSize = 16;

template <> struct System<COMPUTE_SUN_SHADOW> {
    ShaderProgram *program_ = nullptr;
    ShaderProgram *shapeProgram_ = nullptr;
    ShaderProgram *surfaceProgram_ = nullptr;
    ShaderProgram *shapeSurfaceProgram_ = nullptr;
    Buffer *sunShadowFrameDataBuf_ = nullptr;
    Buffer *voxelFrameDataBuf_ = nullptr;
    Buffer *shapeReceiverFrameBuf_ = nullptr;
    Buffer *shapeReceiverFallbackBuf_ = nullptr;
    Buffer *animationParamsBuf_ = nullptr;
    Buffer *shapeProducerFrameBuf_ = nullptr;
    // Created by BAKE_SUN_SHADOW_MAP. Resolved lazily so this lookup
    // links even when the bake system isn't registered.
    Buffer *sunShadowDepthMap_ = nullptr;

    // Smooth camera Z-yaw: main canvas + per-axis voxel canvases,
    // re-resolved every frame in beginTick. Null unless allocated (rotating).
    IREntity::EntityId perAxisCanvasEntity_ = IREntity::kNullEntity;
    C_PerAxisTrixelCanvases *perAxisCanvases_ = nullptr;
    bool perAxisSurfaceLightingEligible_ = false;
    bool cardinalSurfaceLightingEligible_ = false;
    // Debug witness only: logs when the main-canvas skip starts and stops.
    bool mainCanvasDispatchSkipped_ = false;

    // Lazily-resolved voxel-compaction buffers, restored onto
    // slots 25/26 after dispatchPerAxisSunShadow borrows them for its own
    // per-axis cell list. See IRPrefab::PerAxisCanvas::restoreVoxelCompactionSlots.
    Buffer *voxelCompactedBuf_ = nullptr;
    Buffer *voxelIndirectBuf_ = nullptr;

    void tick(
        IREntity::EntityId entity,
        const C_TriangleCanvasTextures &canvasTextures,
        const C_CanvasSunShadow &shadow,
        const C_TrixelCanvasRenderBehavior &behavior
    ) {
        // Skip GUI-only canvases — same rationale as the AO pass.
        if (!behavior.useCameraPositionIso_)
            return;
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

        canvasTextures.getTextureDistances()
            ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
        shadow.getTexture()->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
        canvasTextures.getTextureEntityIds()
            ->bindAsImage(2, TextureAccess::READ_ONLY, TextureFormat::RG32UI);
        sunShadowDepthMap_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_SunShadowDepthMap);
        voxelFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataVoxelToCanvas);
        sunShadowFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataSun);

        const auto &geometry = canvasTextures.shapeGeometry_;
        const bool useShapeReceiver = entity == perAxisCanvasEntity_ && geometry.samplesValid();
        const bool preserveCardinalSurfaces =
            entity == perAxisCanvasEntity_ && cardinalSurfaceLightingEligible_;
        ShaderProgram *activeProgram =
            useShapeReceiver ? (preserveCardinalSurfaces ? shapeSurfaceProgram_ : shapeProgram_)
                             : (preserveCardinalSurfaces ? surfaceProgram_ : program_);
        activeProgram->use();
        if (useShapeReceiver) {
            IRPrefab::detail::bindShapeReceiver(
                geometry,
                canvasTextures.size_,
                *shapeReceiverFrameBuf_
            );
        }

        const bool skipDispatch = preserveCardinalSurfaces && !canvasTextures.holdsQueriedContent();
        if (entity == perAxisCanvasEntity_ && skipDispatch != mainCanvasDispatchSkipped_) {
            mainCanvasDispatchSkipped_ = skipDispatch;
            IR_LOG_INFO(
                "COMPUTE_SUN_SHADOW: main-canvas dispatch {}",
                skipDispatch ? "skipped (cardinal voxel surfaces only)" : "resumed"
            );
        }
        if (!skipDispatch) {
            const int groupsX = IRMath::divCeil(canvasTextures.size_.x, kComputeSunShadowGroupSize);
            const int groupsY = IRMath::divCeil(canvasTextures.size_.y, kComputeSunShadowGroupSize);
            IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
            IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
        }

        // Exact presentation queries the shared sun map directly; its per-axis
        // sampled textures have no consumer. Keep main-canvas shadow production
        // for SDF/text and every fallback route.
        if (entity == perAxisCanvasEntity_ && perAxisCanvases_ != nullptr &&
            perAxisCanvases_->isAllocated() && !perAxisSurfaceLightingEligible_) {
            program_->use();
            dispatchPerAxisSunShadow(*perAxisCanvases_, canvasTextures, shadow);
        }
        if (useShapeReceiver) {
            IRPrefab::detail::restoreShapeReceiver(
                *shapeReceiverFallbackBuf_,
                shapeProducerFrameBuf_,
                animationParamsBuf_
            );
        }
    }

    // The sun depth map and FrameDataSun stay bound across per-axis dispatches.
    void dispatchPerAxisSunShadow(
        C_PerAxisTrixelCanvases &axes,
        const C_TriangleCanvasTextures &mainTextures,
        const C_CanvasSunShadow &mainShadow
    ) {
        {
            IRPrefab::PerAxisCanvas::LightingRouteScope route(
                voxelFrameDataBuf_,
                voxelCompactedBuf_,
                voxelIndirectBuf_,
                axes.size_,
                mainTextures.size_
            );
            IRPrefab::PerAxisCanvas::dispatchPerAxisCells(axes, [&](int axis) {
                auto &tex = axes.axes_[axis];
                tex.distances_.second
                    ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
                tex.sunShadow_.second
                    ->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
            });
            // One barrier after the 3 independent per-axis dispatches (each axis
            // writes its own sun-shadow image texture — disjoint outputs, so
            // dispatch order doesn't matter) so they overlap on the GPU instead
            // of serializing per axis.
            IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
        }
        // Restore the main-canvas image bindings (see the note in
        // system_compute_voxel_ao.hpp — the persistent Metal image-binding table
        // would otherwise dangle when release() frees the per-axis textures).
        mainTextures.getTextureDistances()
            ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
        mainShadow.getTexture()->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
    }

    void beginTick() {
        if (animationParamsBuf_ == nullptr && findSystem(SHAPES_TO_TRIXEL) != kNullSystemId) {
            animationParamsBuf_ = IRRender::getNamedResource<Buffer>("AnimationParamsBuffer");
            shapeProducerFrameBuf_ = IRRender::getNamedResource<Buffer>("ShapesFrameDataBuffer");
        }
        // BAKE_SUN_SHADOW_MAP owns FrameDataSun uploads — its
        // tick is the authoritative writer for the full struct
        // (sun direction + basis + AABB + flags), uploaded via
        // subData after BAKE's beginTick populates frameData_.
        // This pass runs after BAKE so the struct is fresh.
        if (sunShadowDepthMap_ == nullptr) {
            sunShadowDepthMap_ = IRRender::getNamedResource<Buffer>("SunShadowDepthMap");
        }

        // Resolve the main canvas + its per-axis voxel canvases.
        perAxisCanvasEntity_ = IRRender::getCanvas("main");
        perAxisCanvases_ = nullptr;
        if (perAxisCanvasEntity_ != IREntity::kNullEntity) {
            auto perAxis =
                IREntity::getComponentOptional<C_PerAxisTrixelCanvases>(perAxisCanvasEntity_);
            if (perAxis.has_value()) {
                perAxisCanvases_ = perAxis.value();
            }
        }
        const auto route = IRPrefab::PerAxisSurfaceLighting::resolveRoute(perAxisCanvasEntity_);
        perAxisSurfaceLightingEligible_ =
            perAxisCanvases_ != nullptr && perAxisCanvases_->isAllocated() &&
            findSystem(LIGHTING_TO_TRIXEL) != kNullSystemId && route.perAxis_;
        cardinalSurfaceLightingEligible_ = route.cardinal_ && !route.cardinalGlitch_;
        // Lighting's cardinal skip also needs zero residual yaw. The route
        // reaches that only through per-axis allocation on rotating frames, so a
        // main canvas without C_PerAxisTrixelCanvases would break the chain.
        IR_ASSERT(
            !cardinalSurfaceLightingEligible_ ||
                (IRPrefab::Camera::getResidualYaw() == 0.0f && !IRRender::isSpatialViewEnabled()),
            "COMPUTE_SUN_SHADOW: surface variant on a frame whose lighting samples cardinal "
            "voxel surfaces (non-zero residual yaw or spatial view)"
        );
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "ComputeSunShadowProgram",
            std::vector{ShaderStage{IRRender::kFileCompComputeSunShadow, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<ShaderProgram>(
            "ComputeSunShadowShapesProgram",
            std::vector{ShaderStage{IRRender::kFileCompComputeSunShadowShapes, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<ShaderProgram>(
            "ComputeSunShadowSurfaceProgram",
            std::vector{
                ShaderStage{IRRender::kFileCompComputeSunShadowSurface, ShaderType::COMPUTE}
            }
        );
        IRRender::createNamedResource<ShaderProgram>(
            "ComputeSunShadowShapesSurfaceProgram",
            std::vector{
                ShaderStage{IRRender::kFileCompComputeSunShadowShapesSurface, ShaderType::COMPUTE}
            }
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
            "ShapeReceiverFrameData",
            nullptr,
            sizeof(GPUShapesFrameData),
            BUFFER_STORAGE_DYNAMIC
        );
        const GPUShapeDescriptor fallback{};
        IRRender::createNamedResource<Buffer>(
            "ShapeReceiverFallback",
            &fallback,
            sizeof(fallback),
            BUFFER_STORAGE_DYNAMIC
        );

        SystemId systemId = registerSystem<
            COMPUTE_SUN_SHADOW,
            C_TriangleCanvasTextures,
            C_CanvasSunShadow,
            C_TrixelCanvasRenderBehavior>("ComputeSunShadow");
        auto *p = getSystemParams<System<COMPUTE_SUN_SHADOW>>(systemId);
        p->shapeReceiverFrameBuf_ = IRRender::getNamedResource<Buffer>("ShapeReceiverFrameData");
        p->shapeReceiverFallbackBuf_ = IRRender::getNamedResource<Buffer>("ShapeReceiverFallback");
        p->program_ = IRRender::getNamedResource<ShaderProgram>("ComputeSunShadowProgram");
        p->shapeProgram_ =
            IRRender::getNamedResource<ShaderProgram>("ComputeSunShadowShapesProgram");
        p->surfaceProgram_ =
            IRRender::getNamedResource<ShaderProgram>("ComputeSunShadowSurfaceProgram");
        p->shapeSurfaceProgram_ =
            IRRender::getNamedResource<ShaderProgram>("ComputeSunShadowShapesSurfaceProgram");
        p->sunShadowFrameDataBuf_ = IRRender::getNamedResource<Buffer>("ComputeSunShadowFrameData");
        p->voxelFrameDataBuf_ = IRRender::getNamedResource<Buffer>("SingleVoxelFrameData");
        IRRender::tagGpuStage(systemId, "computeSunShadow");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_COMPUTE_SUN_SHADOW_H */
