#ifndef SYSTEM_FOG_TO_TRIXEL_H
#define SYSTEM_FOG_TO_TRIXEL_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

// Must match local_size in c_fog_to_trixel.glsl / .metal.
constexpr int kFogToTrixelGroupSize = 16;

// Screen-space fog-of-war pass. Sits between LIGHTING_TO_TRIXEL (which
// modulates by AO × sun-shadow) and TRIXEL_TO_TRIXEL (compositing) so
// fog masks the *lit* canvas — explored cells fade their lighting too,
// not just the base albedo.
//
// GUI canvases skip this pass via the same
// `useCameraPositionIso_ == false` early-return that lighting uses; GUI
// pixels have no associated world position and would render garbage if
// the pos3D recovery ran on them.
//
// CPU→GPU sync: the dirty-gated `subImage2D` upload now lives in
// VOXEL_TO_TRIXEL_STAGE_1 (#2008), which both performs the column cull
// (it needs current-frame fog) and runs earlier in the pipeline. This
// pass is read-only on the already-uploaded fog texture — hence the
// const fog param — so the cull and this post-process always see the
// same fog with no one-frame lag.
template <> struct System<FOG_TO_TRIXEL> {
    ShaderProgram *program_ = nullptr;
    Buffer *voxelFrameDataBuf_ = nullptr;
    // Tiny per-canvas UBO carrying the live analytic vision circles. Uploaded
    // every frame (a few vec4 + a count) — small and unconditional, so unlike
    // the fog texture it needs no dirty flag.
    Buffer *observerBuf_ = nullptr;

    void tick(
        const C_TriangleCanvasTextures &canvasTextures,
        const C_TrixelCanvasRenderBehavior &behavior,
        const C_CanvasFogOfWar &fog
    ) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
        if (!behavior.useCameraPositionIso_) {
            return;
        }

        // Live analytic vision circles. Small, GPU-read-only, re-authored by
        // gameplay each frame — uploaded unconditionally (no dirty flag). The
        // shader max-combines these with the grid memory above. (The grid fog
        // texture itself is uploaded earlier in VOXEL_TO_TRIXEL_STAGE_1 (#2008);
        // this pass only reads it — hence the const fog param.) The shader's
        // cross-section cap is pure per-pixel geometry (face axis + radial
        // band), so no occupancy source is bound for this dispatch.
        observerBuf_->subData(0, sizeof(FrameDataFogObservers), &fog.observers_);

        canvasTextures.getTextureColors()
            ->bindAsImage(0, TextureAccess::READ_WRITE, TextureFormat::RGBA8);
        canvasTextures.getTextureDistances()
            ->bindAsImage(1, TextureAccess::READ_ONLY, TextureFormat::R32I);
        fog.getTexture()->bindAsImage(2, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
        // FrameDataVoxelToTrixel carries frameCanvasOffset /
        // trixelCanvasOffsetZ1 / voxelRenderOptions for the iso pixel → world
        // pos3D recovery, plus visibleFaceIds for the cross-section cap's
        // slot → face-axis resolution. Each pass that consumes it re-binds
        // explicitly because any intervening dispatch may have rebound
        // binding 7.
        voxelFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataVoxelToCanvas);
        observerBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FogObservers);

        const int groupsX = IRMath::divCeil(canvasTextures.size_.x, kFogToTrixelGroupSize);
        const int groupsY = IRMath::divCeil(canvasTextures.size_.y, kFogToTrixelGroupSize);
        IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
    }

    void beginTick() {
        program_->use();
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "FogToTrixelProgram",
            std::vector{ShaderStage{IRRender::kFileCompFogToTrixel, ShaderType::COMPUTE}}
        );

        // Per-canvas analytic vision-circle UBO (binding kBufferIndex_FogObservers).
        IRRender::createNamedResource<Buffer>(
            "FogObserverData",
            nullptr,
            sizeof(FrameDataFogObservers),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_FogObservers
        );

        SystemId systemId = registerSystem<
            FOG_TO_TRIXEL,
            C_TriangleCanvasTextures,
            C_TrixelCanvasRenderBehavior,
            C_CanvasFogOfWar>("FogToTrixel");
        auto *p = getSystemParams<System<FOG_TO_TRIXEL>>(systemId);
        p->program_ = IRRender::getNamedResource<ShaderProgram>("FogToTrixelProgram");
        p->voxelFrameDataBuf_ = IRRender::getNamedResource<Buffer>("SingleVoxelFrameData");
        p->observerBuf_ = IRRender::getNamedResource<Buffer>("FogObserverData");
        IRRender::tagGpuStage(systemId, "fogToTrixel");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FOG_TO_TRIXEL_H */
