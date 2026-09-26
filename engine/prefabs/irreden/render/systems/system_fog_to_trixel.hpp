#ifndef SYSTEM_FOG_TO_TRIXEL_H
#define SYSTEM_FOG_TO_TRIXEL_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <cstddef>
#include <cstdlib>

#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_substage_timing.hpp>
#include <irreden/render/per_axis_canvas.hpp>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

// Must match local_size in c_fog_to_trixel_body.glsl / .metal.
constexpr int kFogToTrixelGroupSize = 16;

// Screen-space fog-of-war pass. Sits between LIGHTING_TO_TRIXEL (which
// modulates by AO × sun-shadow) and TRIXEL_TO_TRIXEL (compositing) so
// fog masks the *lit* canvas — explored cells fade their lighting too,
// not just the base albedo. On the main canvas it also paints the per-axis
// canvases and the per-axis overflow lane (dispatchPerAxisFog).
//
// GUI canvases skip this pass via the same
// `useCameraPositionIso_ == false` early-return that lighting uses; GUI
// pixels have no associated world position and would render garbage if
// the pos3D recovery ran on them.
//
// CPU→GPU sync: the dirty-gated `subImage2D` upload lives in
// VOXEL_TO_TRIXEL_STAGE_1, which both performs the column cull (it needs
// current-frame fog) and runs earlier in the pipeline. This pass is
// read-only on the already-uploaded fog texture — hence the const fog
// param — so the cull and this post-process always see the same fog.
template <> struct System<FOG_TO_TRIXEL> {
    // The hard-gate kernel; every per-axis dispatch uses it.
    ShaderProgram *program_ = nullptr;
    // The main-canvas kernel variant carrying the smooth line-of-sight gate,
    // bound only while a smooth source is live: compiled in, the gate slows
    // the pass even on pixels no gated source reaches.
    ShaderProgram *losSmoothProgram_ = nullptr;
    ShaderProgram *overflowProgram_ = nullptr;
    Buffer *voxelFrameDataBuf_ = nullptr;
    // Tiny per-canvas UBO carrying the live analytic vision circles. Uploaded
    // every frame (a few vec4 + a count) — small and unconditional, so unlike
    // the fog texture it needs no dirty flag.
    Buffer *observerBuf_ = nullptr;
    Buffer *voxelActiveMaskBuf_ = nullptr;
    Buffer *voxelCompactedBuf_ = nullptr;
    Buffer *voxelIndirectBuf_ = nullptr;
    bool overflowFogDisabled_ = false;
    IREntity::EntityId perAxisCanvasEntity_ = IREntity::kNullEntity;
    C_PerAxisTrixelCanvases *perAxisCanvases_ = nullptr;

    void tick(
        IREntity::EntityId entity,
        const C_TriangleCanvasTextures &canvasTextures,
        const C_TrixelCanvasRenderBehavior &behavior,
        const C_CanvasFogOfWar &fog
    ) {
        if (!behavior.useCameraPositionIso_) {
            return;
        }
        IR_PROFILE_SCOPE("fogToTrixel");

        (fog.observers_.hasSmoothLineOfSightSource() ? losSmoothProgram_ : program_)->use();
        observerBuf_->subData(0, sizeof(FrameDataFogObservers), &fog.observers_);
        canvasTextures.getTextureColors()
            ->bindAsImage(0, TextureAccess::READ_WRITE, TextureFormat::RGBA8);
        canvasTextures.getTextureDistances()
            ->bindAsImage(1, TextureAccess::READ_ONLY, TextureFormat::R32I);
        fog.getTexture()->bindAsImage(2, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
        // The fog whole-body carrier bit rides the entity-id channel. On Metal
        // this evicts whatever was resident in slot 3; every later unit-3 user
        // rebinds it inside its own tick.
        canvasTextures.getTextureEntityIds()
            ->bindAsImage(3, TextureAccess::READ_ONLY, TextureFormat::RG32UI);
        // Line-of-sight horizons, read only for gated sources. On Metal this
        // evicts LIGHTING_TO_TRIXEL's sun-shadow input from slot 4; lighting
        // rebinds it inside its own tick.
        fog.getLosTexture()->bindAsImage(4, TextureAccess::READ_ONLY, TextureFormat::RGBA32F);
        // FrameDataVoxelToTrixel carries frameCanvasOffset /
        // trixelCanvasOffsetZ1 / voxelRenderOptions for the iso pixel → world
        // pos3D recovery, plus visibleFaceIds for the cross-section cap's
        // slot → face-axis resolution. Each pass that consumes it re-binds
        // explicitly because any intervening dispatch may have rebound
        // binding 7.
        voxelFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataVoxelToCanvas);
        observerBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FogObservers);

        {
            GpuSubStageScope mainScope("fogToTrixel");
            const int groupsX = IRMath::divCeil(canvasTextures.size_.x, kFogToTrixelGroupSize);
            const int groupsY = IRMath::divCeil(canvasTextures.size_.y, kFogToTrixelGroupSize);
            IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
            IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
        }

        if (entity == perAxisCanvasEntity_ && perAxisCanvases_ != nullptr &&
            perAxisCanvases_->isAllocated()) {
            dispatchPerAxisFog(*perAxisCanvases_, canvasTextures, fog);
        }
    }

    void dispatchPerAxisFog(
        C_PerAxisTrixelCanvases &axes,
        const C_TriangleCanvasTextures &mainTextures,
        const C_CanvasFogOfWar &fog
    ) {
        program_->use();
        {
            IRPrefab::PerAxisCanvas::LightingRouteScope route(
                voxelFrameDataBuf_,
                voxelCompactedBuf_,
                voxelIndirectBuf_
            );
            {
                GpuSubStageScope perAxisScope("fogPerAxis");
                IRPrefab::PerAxisCanvas::dispatchPerAxisCells(axes, [&](int axis) {
                    auto &textures = axes.axes_[axis];
                    textures.colors_.second
                        ->bindAsImage(0, TextureAccess::READ_WRITE, TextureFormat::RGBA8);
                    textures.distances_.second
                        ->bindAsImage(1, TextureAccess::READ_ONLY, TextureFormat::R32I);
                    textures.entityIds_.second
                        ->bindAsImage(3, TextureAccess::READ_ONLY, TextureFormat::RG32UI);
                });
                IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
            }
            dispatchOverflowFog(axes, mainTextures.size_);
        }

        if (voxelActiveMaskBuf_ == nullptr) {
            voxelActiveMaskBuf_ = IRRender::getNamedResource<Buffer>("VoxelActiveMaskBuffer");
        }
        voxelActiveMaskBuf_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_VoxelActiveMask);
        mainTextures.getTextureColors()
            ->bindAsImage(0, TextureAccess::READ_WRITE, TextureFormat::RGBA8);
        mainTextures.getTextureDistances()
            ->bindAsImage(1, TextureAccess::READ_ONLY, TextureFormat::R32I);
        fog.getTexture()->bindAsImage(2, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
        mainTextures.getTextureEntityIds()
            ->bindAsImage(3, TextureAccess::READ_ONLY, TextureFormat::RG32UI);
    }

    void dispatchOverflowFog(C_PerAxisTrixelCanvases &axes, ivec2 mainCanvasSize) {
        if (overflowFogDisabled_ || axes.overflowCap_ <= 0 || axes.winnerIds_.second == nullptr) {
            return;
        }
        GpuSubStageScope overflowScope("fogOverflow");
        overflowProgram_->use();
        const ivec4 overflowLayout(
            axes.viewMaskBaseUints_,
            axes.ctrlBaseUints_,
            axes.entriesBaseUints_,
            axes.overflowCap_
        );
        voxelFrameDataBuf_->subData(
            offsetof(FrameDataVoxelToCanvas, overflowScratchLayout_),
            sizeof(ivec4),
            &overflowLayout
        );
        // Entries are keyed on the per-axis store's origin anchor, which the
        // kernel derives from canvasSizePixels_. This tick's UBO carries the
        // main canvas size, so publish the store size for the dispatch.
        voxelFrameDataBuf_->subData(
            offsetof(FrameDataVoxelToCanvas, canvasSizePixels_),
            sizeof(ivec2),
            &axes.size_
        );
        axes.winnerIds_.second->bindBase(
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_OverflowLightingScratch
        );
        IRRender::device()->dispatchComputeIndirect(
            axes.cellIndirect_.second,
            kOverflowLightingDispatchArgsOffsetBytes
        );
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
        voxelFrameDataBuf_->subData(
            offsetof(FrameDataVoxelToCanvas, canvasSizePixels_),
            sizeof(ivec2),
            &mainCanvasSize
        );
    }

    void beginTick() {
        perAxisCanvasEntity_ = IRRender::getCanvas("main");
        perAxisCanvases_ = nullptr;
        if (perAxisCanvasEntity_ == IREntity::kNullEntity) {
            return;
        }
        auto perAxis =
            IREntity::getComponentOptional<C_PerAxisTrixelCanvases>(perAxisCanvasEntity_);
        if (perAxis.has_value()) {
            perAxisCanvases_ = perAxis.value();
        }
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "FogToTrixelProgram",
            std::vector{ShaderStage{IRRender::kFileCompFogToTrixel, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<ShaderProgram>(
            "FogToTrixelLosSmoothProgram",
            std::vector{
                ShaderStage{IRRender::kFileCompFogToTrixelLosSmooth, ShaderType::COMPUTE}
            }
        );
        IRRender::createNamedResource<ShaderProgram>(
            "FogOverflowFacesProgram",
            std::vector{ShaderStage{IRRender::kFileCompFogOverflowFaces, ShaderType::COMPUTE}}
        );
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
        auto *params = getSystemParams<System<FOG_TO_TRIXEL>>(systemId);
        params->program_ = IRRender::getNamedResource<ShaderProgram>("FogToTrixelProgram");
        params->losSmoothProgram_ =
            IRRender::getNamedResource<ShaderProgram>("FogToTrixelLosSmoothProgram");
        params->overflowProgram_ =
            IRRender::getNamedResource<ShaderProgram>("FogOverflowFacesProgram");
        params->voxelFrameDataBuf_ = IRRender::getNamedResource<Buffer>("SingleVoxelFrameData");
        params->observerBuf_ = IRRender::getNamedResource<Buffer>("FogObserverData");
        params->overflowFogDisabled_ = std::getenv("IR_OVERFLOW_FOG_DISABLE") != nullptr;
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FOG_TO_TRIXEL_H */
