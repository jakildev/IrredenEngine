#ifndef SYSTEM_RESOLVE_PER_AXIS_SCREEN_DEPTH_H
#define SYSTEM_RESOLVE_PER_AXIS_SCREEN_DEPTH_H

// Smooth camera Z-yaw — per-axis sun-shadow resolve (#1435).
//
// Restores faithful per-axis voxel sun-shadow CASTING under continuous Z-yaw
// (deferred by #1380's option C, which stopped per-axis canvases casting to
// avoid cross-face self-occlusion in the shared sun depth map). This stage
// re-projects the three face-local per-axis voxel canvases into ONE
// screen-space front-most iso-depth texture laid out exactly like the main
// canvas distance texture, so BAKE_SUN_SHADOW_MAP casts them through its
// EXISTING cardinal recovery (trixelCanvasPixelToWorld3D) — the per-screen-pixel
// flattening that the raw face-local representation lacks. See
// docs/design/per-axis-sun-shadow-resolve.md.
//
// Pipeline order: after the geometry + per-axis raster stages (the per-axis
// distance textures must be populated) and before BAKE_SUN_SHADOW_MAP (which
// reads the resolve texture). No-ops at a cardinal — the per-axis canvases are
// only allocated at non-zero residual yaw, so cardinal output stays
// byte-identical.
//
// Two compute passes (both backends, no texture atomics — Metal lacks portable
// image-atomic syntax):
//   1. scatter (×3 axes) → imageAtomicMin into a main-canvas-sized scratch SSBO.
//   2. blit → materialize the scratch into the resolve R32I texture and reset
//      the scratch to the empty sentinel for next frame.

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/per_axis_canvas.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

#include <cstddef>
#include <vector>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

// Matches local_size_{x,y} in c_resolve_per_axis_screen_depth.glsl /
// c_resolve_per_axis_blit.glsl.
constexpr int kResolvePerAxisGroupSize = 16;

template <> struct System<RESOLVE_PER_AXIS_SCREEN_DEPTH> {
    ShaderProgram *scatterProgram_ = nullptr;
    ShaderProgram *blitProgram_ = nullptr;
    Buffer *voxelFrameDataBuf_ = nullptr;

    // Screen-space (main-canvas-sized) front-most iso-depth scratch. A buffer
    // not a texture because Metal has no portable image-atomic syntax — same
    // rationale as c_voxel_to_trixel_stage_1.metal's distance scratch.
    // (Re)allocated to the main canvas size; aliases slot 28 during this stage
    // only (BAKE rebinds slot 28 to the sun depth map afterward).
    ResourceId scratchId_ = 0;
    Buffer *scratch_ = nullptr;
    ivec2 scratchSize_ = ivec2(0, 0);

    // Main canvas + its per-axis voxel canvases, re-resolved every frame in
    // beginTick. Null unless the per-axis canvases are allocated (rotating).
    IREntity::EntityId perAxisCanvasEntity_ = IREntity::kNullEntity;
    C_PerAxisTrixelCanvases *perAxisCanvases_ = nullptr;

    // Lazily-resolved voxel-compaction buffers (#1961/#2256), restored onto
    // slots 25/26 after the scatter pass borrows them for its own per-axis
    // cell list. See IRPrefab::PerAxisCanvas::restoreVoxelCompactionSlots.
    Buffer *voxelCompactedBuf_ = nullptr;
    Buffer *voxelIndirectBuf_ = nullptr;

    // (Re)allocate the scratch SSBO to @p mainSize and seed it to the empty
    // sentinel. Runs only on a size change (window/canvas resize), not per
    // frame — the blit keeps the scratch reset thereafter.
    void ensureScratch(ivec2 mainSize) {
        if (scratch_ != nullptr && scratchSize_ == mainSize) {
            return;
        }
        if (scratch_ != nullptr) {
            IRRender::destroyResource<Buffer>(scratchId_);
            scratch_ = nullptr;
        }
        const std::size_t count =
            static_cast<std::size_t>(mainSize.x) * static_cast<std::size_t>(mainSize.y);
        auto created = IRRender::createResource<Buffer>(
            nullptr,
            count * sizeof(std::int32_t),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_PerAxisResolveScratch
        );
        scratchId_ = created.first;
        scratch_ = created.second;
        scratchSize_ = mainSize;
        std::vector<std::int32_t> seed(
            count,
            static_cast<std::int32_t>(IRConstants::kTrixelDistanceMaxDistance)
        );
        scratch_->subData(0, count * sizeof(std::int32_t), seed.data());
    }

    void tick(
        IREntity::EntityId entity,
        const C_TriangleCanvasTextures &canvasTextures,
        const C_TrixelCanvasRenderBehavior &behavior
    ) {
        // Only the world canvas drives per-axis voxel canvases.
        if (!behavior.useCameraPositionIso_) {
            return;
        }
        if (entity != perAxisCanvasEntity_ || perAxisCanvases_ == nullptr ||
            !perAxisCanvases_->isAllocated()) {
            return;
        }
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

        const ivec2 mainSize = canvasTextures.size_;
        ensureScratch(mainSize);

        // Guarantee the scatter reads the MAIN canvas size from the UBO — a
        // per-axis store dispatch may have left a per-axis size in this field.
        // The bake never reads canvasSizePixels_, so this is safe to leave set.
        // Also patch the #1431-capped subdivision density — the store restored
        // the uncapped value on exit, but the scatter inverts face-plane origins
        // through effectiveTrixelSubdivisionScale(voxelRenderOptions.y), so it
        // must use the same capped density the store wrote. Restored below.
        voxelFrameDataBuf_->subData(
            offsetof(FrameDataVoxelToCanvas, canvasSizePixels_),
            sizeof(ivec2),
            &mainSize
        );
        IRPrefab::PerAxisCanvas::setUboSubdivisionDensity(
            voxelFrameDataBuf_,
            IRPrefab::PerAxisCanvas::subdivisionDensity()
        );
        voxelFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataVoxelToCanvas);

        // Pass 1 — scatter the three face-local per-axis canvases into the
        // scratch (front-most per screen pixel via atomicMin). #2256: dispatch
        // indirectly over only each axis's compacted occupied cells (filled by the
        // STAGE_1 per-axis compaction) instead of sweeping the full worst-case grid.
        scatterProgram_->use();
        scratch_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_PerAxisResolveScratch);
        IRPrefab::PerAxisCanvas::dispatchPerAxisCells(*perAxisCanvases_, [&](int axis) {
            perAxisCanvases_->axes_[axis]
                .distances_.second->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
        });
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);

        // Pass 2 — blit the scratch into the resolve texture BAKE reads, and
        // reset the scratch to the empty sentinel for next frame.
        blitProgram_->use();
        scratch_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_PerAxisResolveScratch);
        perAxisCanvases_->resolveDepth_.second
            ->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::R32I);
        const int mainGroupsX = IRMath::divCeil(mainSize.x, kResolvePerAxisGroupSize);
        const int mainGroupsY = IRMath::divCeil(mainSize.y, kResolvePerAxisGroupSize);
        IRRender::device()->dispatchCompute(mainGroupsX, mainGroupsY, 1);
        // ALL: the resolve texture is read as an image by BAKE, and the scratch
        // reset must be visible to next frame's scatter.
        IRRender::device()->memoryBarrier(BarrierType::ALL);
        // Restore the uncapped density so downstream BAKE recovers the main
        // canvas distances at the correct single-canvas scale.
        IRPrefab::PerAxisCanvas::setUboSubdivisionDensity(
            voxelFrameDataBuf_,
            IRRender::getVoxelRenderEffectiveSubdivisions()
        );
        // Restore slots 25/26 to the voxel-compaction buffers (#1961/#2256) the
        // scatter pass above borrowed via bindRange — see the restore-slots note
        // in system_compute_voxel_ao.hpp for the corruption mode this avoids.
        IRPrefab::PerAxisCanvas::restoreVoxelCompactionSlots(voxelCompactedBuf_, voxelIndirectBuf_);
    }

    void beginTick() {
        perAxisCanvasEntity_ = IRRender::getCanvas("main");
        perAxisCanvases_ = nullptr;
        if (perAxisCanvasEntity_ != IREntity::kNullEntity) {
            auto perAxis =
                IREntity::getComponentOptional<C_PerAxisTrixelCanvases>(perAxisCanvasEntity_);
            if (perAxis.has_value()) {
                perAxisCanvases_ = perAxis.value();
            }
        }
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "ResolvePerAxisScreenDepthProgram",
            std::vector{
                ShaderStage{IRRender::kFileCompResolvePerAxisScreenDepth, ShaderType::COMPUTE}
            }
        );
        IRRender::createNamedResource<ShaderProgram>(
            "ResolvePerAxisBlitProgram",
            std::vector{ShaderStage{IRRender::kFileCompResolvePerAxisBlit, ShaderType::COMPUTE}}
        );

        SystemId systemId = registerSystem<
            RESOLVE_PER_AXIS_SCREEN_DEPTH,
            C_TriangleCanvasTextures,
            C_TrixelCanvasRenderBehavior>("ResolvePerAxisScreenDepth");
        auto *p = getSystemParams<System<RESOLVE_PER_AXIS_SCREEN_DEPTH>>(systemId);
        p->scatterProgram_ =
            IRRender::getNamedResource<ShaderProgram>("ResolvePerAxisScreenDepthProgram");
        p->blitProgram_ = IRRender::getNamedResource<ShaderProgram>("ResolvePerAxisBlitProgram");
        p->voxelFrameDataBuf_ = IRRender::getNamedResource<Buffer>("SingleVoxelFrameData");
        IRRender::tagGpuStage(systemId, "resolvePerAxisScreenDepth");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_RESOLVE_PER_AXIS_SCREEN_DEPTH_H */
