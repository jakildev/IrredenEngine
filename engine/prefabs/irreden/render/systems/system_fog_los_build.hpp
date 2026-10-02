#ifndef SYSTEM_FOG_LOS_BUILD_H
#define SYSTEM_FOG_LOS_BUILD_H

// Builds the fog line-of-sight column field (`C_CanvasFogOfWar::losColumnTops_`)
// of the active grid canvas and uploads it to `losTexture_` for FOG_TO_TRIXEL.
// RENDER, before FOG_TO_TRIXEL, in its own pipeline group. With no gated
// source it returns before touching anything; with one it rebuilds the field
// and its pyramid at the frame's raster lattice, anchored on the fog window
// the frame's gather is about to upload, and uploads the whole texture each
// frame, then publishes the observers and anchor it built for
// FOG_REVEAL_EVAL's next UPDATE. The model: `component_canvas_fog_of_war.hpp`.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/fog_line_of_sight.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/render/canvas_coverage.hpp>

namespace IRSystem {

template <> struct System<FOG_LOS_BUILD> {
    IREntity::EntityId activeCanvas_ = IREntity::kNullEntity;
    IRPrefab::Fog::LosRasterFrame frame_{};

    void beginTick() {
        activeCanvas_ = IRRender::getActiveCanvasEntityOrNull();
        frame_ = IRPrefab::Fog::activeLosRasterFrame();
    }

    void tick(
        IREntity::EntityId canvas,
        const IRComponents::C_VoxelPool &pool,
        IRComponents::C_CanvasFogOfWar &fog,
        const IRComponents::C_TrixelCanvasRenderBehavior &behavior,
        const IRComponents::C_TriangleCanvasTextures &textures
    ) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
        if (!behavior.useCameraPositionIso_ || canvas != activeCanvas_ ||
            fog.observers_.losSourceMask_ == 0) {
            return;
        }
        IRRender::FogLosBuildTiming &phaseTiming = IRRender::fogLosBuildTiming();
        // The gather writes this frame's window origin after this system runs,
        // from the same camera state; the fog pass anchors the field on it.
        const IRMath::ivec2 fieldMin = IRComponents::FogLosColumnField::fieldMinForWindow(
            IRPrefab::Fog::detail::cameraWindowOrigin(
                fog.windowEdge_,
                IRPrefab::CanvasCoverage::logicalSize(canvas, textures.size_)
            ),
            fog.windowEdge_
        );
        {
            IRRender::ScopedCpuPhaseTimer timer{phaseTiming.build_};
            IR_PROFILE_BLOCK("FogLosBuild::Columns", IR_PROFILER_COLOR_RENDER);
            IRPrefab::Fog::rasterizeLosColumns(pool, canvas, frame_, fieldMin, fog.losColumnTops_);
        }
        fog.losPublishedObservers_ = fog.observers_;
        fog.losPublishedFieldMin_ = fieldMin;
        fog.losPublished_ = true;
        {
            IRRender::ScopedCpuPhaseTimer timer{phaseTiming.upload_};
            IR_PROFILE_BLOCK("FogLosBuild::Upload", IR_PROFILER_COLOR_RENDER);
            fog.getLosTexture()->subImage2D(
                0,
                0,
                IRComponents::kFogLosTextureSize,
                IRComponents::kFogLosTextureHeight,
                IRRender::PixelDataFormat::RGBA,
                IRRender::PixelDataType::FLOAT32,
                fog.losColumnTops_.data()
            );
        }
    }

    static SystemId create() {
        return registerSystem<
            FOG_LOS_BUILD,
            IRComponents::C_VoxelPool,
            IRComponents::C_CanvasFogOfWar,
            IRComponents::C_TrixelCanvasRenderBehavior,
            IRComponents::C_TriangleCanvasTextures,
            AlsoReads<
                IRComponents::C_ShapeDescriptor,
                IRComponents::C_LightBlocker,
                IRComponents::C_WorldTransform>,
            MainThread>("FogLosBuild");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FOG_LOS_BUILD_H */
