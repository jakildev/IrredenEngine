#ifndef SYSTEM_COMPUTE_VOXEL_AO_H
#define SYSTEM_COMPUTE_VOXEL_AO_H

// Per-pixel ambient-occlusion compute pass. Reads the canvas distance
// texture, samples four face-tangent neighbour pixels for each face
// pixel, and writes a grayscale AO factor to the canvas AO texture.
//
// Pipeline order constraint: must run after all geometry stages
// (VOXEL_TO_TRIXEL_STAGE_1, SHAPES_TO_TRIXEL) so the distance texture is
// fully populated, and before LIGHTING_TO_TRIXEL which consumes the AO
// texture.

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/render/per_axis_canvas.hpp>
#include <irreden/render/voxel_frame_data.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_substage_timing.hpp>

#include <cstddef>
#include <cstdint>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

// Matches local_size_{x,y} in c_compute_voxel_ao.glsl.
constexpr int kComputeVoxelAOGroupSize = 16;

template <> struct System<COMPUTE_VOXEL_AO> {
    ShaderProgram *program_ = nullptr;
    Buffer *voxelFrameDataBuf_ = nullptr;
    // `ComputeSunShadowFrameData` is created by COMPUTE_SUN_SHADOW,
    // which is constructed AFTER AO in pipeline registration order.
    // Resolved lazily on the first beginTick (which fires before any
    // per-entity tick), so the per-entity tick can use it directly.
    Buffer *sunFrameDataBuf_ = nullptr;

    // Smooth camera Z-yaw (#1311): the main canvas + its per-axis voxel
    // canvases, re-resolved every frame in beginTick (never held across frames,
    // per .claude/rules/cpp-ecs.md). Null unless the main canvas owns the
    // component AND it is currently allocated (camera at a non-cardinal yaw).
    IREntity::EntityId perAxisCanvasEntity_ = IREntity::kNullEntity;
    C_PerAxisTrixelCanvases *perAxisCanvases_ = nullptr;

    // Lazily-resolved voxel-compaction buffers (#1961/#2256), restored onto
    // slots 25/26 after dispatchPerAxisAO borrows them for its own per-axis
    // cell list. See IRPrefab::PerAxisCanvas::restoreVoxelCompactionSlots.
    Buffer *voxelCompactedBuf_ = nullptr;
    Buffer *voxelIndirectBuf_ = nullptr;

    // Per-pass voxel-frame author/restore for multi-lit-canvas scenes
    // (re-voxelize P4 / #1558). The shared voxel UBO (binding 7) is authored
    // per canvas by VOXEL_TO_TRIXEL_STAGE_1, but only the LAST canvas it
    // processes stays resident — so a second lit canvas (a detached re-voxelize
    // solid) would read the main canvas's visible-triplet / detached flag and
    // mis-shade at non-zero camera yaw. AO re-authors the iterating canvas's
    // frame before its dispatch and restores the main canvas's at endTick so
    // BAKE / COMPUTE_SUN_SHADOW downstream keep reading the world frame.
    // Resolved once per frame in beginTick; never held across frames.
    FrameDataVoxelToCanvas scratchVoxelFrame_{};
    const C_TriangleCanvasTextures *mainCanvasTextures_ = nullptr;
    const C_VoxelPool *mainVoxelPool_ = nullptr;
    const C_CanvasLocalRotation *mainCanvasRotation_ = nullptr;

    void tick(
        IREntity::EntityId entity,
        const C_TriangleCanvasTextures &canvasTextures,
        const C_CanvasAOTexture &ao,
        const C_TrixelCanvasRenderBehavior &behavior
    ) {
        // GUI canvases carry no world-space geometry, so AO would
        // read garbage iso coords — skip.
        if (!behavior.useCameraPositionIso_)
            return;
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
        // CPU histogram bracket — this system is not observer-tagged (#2281),
        // so without it the perf overlay's VOXEL-AO CPU row reads 0.0. The
        // IR_PROFILE_FUNCTION above feeds easy_profiler, not cpuFrameHistogram.
        IR_PROFILE_SCOPE("computeVoxelAO");

        // Author THIS canvas's voxel frame data so AO shades it with its own
        // visible-triplet / detached state (#1558). No-op for a canvas with no
        // voxel pool (a pure-SDF lit canvas keeps the resident frame, unchanged).
        // getComponentOptional on the iterating canvas is the canvas-iteration
        // pattern (few canvases; cf. system_trixel_to_framebuffer.hpp:63), not
        // the per-voxel ECS footgun.
        authorIteratingCanvasVoxelFrame(
            scratchVoxelFrame_,
            voxelFrameDataBuf_,
            entity,
            canvasTextures
        );

        {
            // Sub-scope (#2281): the main-canvas AO dispatch only — the system
            // is untagged from the per-system observer so the per-axis rows
            // below stay separable (a sub-scope reuses the observer's slot).
            GpuSubStageScope aoScope("computeVoxelAO");
            canvasTextures.getTextureDistances()
                ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
            ao.getTexture()->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
            voxelFrameDataBuf_->bindBase(
                BufferTarget::UNIFORM,
                kBufferIndex_FrameDataVoxelToCanvas
            );
            sunFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataSun);

            const int groupsX = IRMath::divCeil(canvasTextures.size_.x, kComputeVoxelAOGroupSize);
            const int groupsY = IRMath::divCeil(canvasTextures.size_.y, kComputeVoxelAOGroupSize);
            IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
            IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
        }

        // Smooth camera Z-yaw (#1311): compute AO for each per-axis voxel
        // canvas so the framebuffer scatter composites LIT voxels while rotating.
        // Only the main canvas owns them, and only while at a non-cardinal yaw.
        if (entity == perAxisCanvasEntity_ && perAxisCanvases_ != nullptr &&
            perAxisCanvases_->isAllocated()) {
            GpuSubStageScope perAxisScope("computeVoxelAoPerAxis");
            dispatchPerAxisAO(*perAxisCanvases_, canvasTextures, ao);
        }
    }

    // Run the AO compute over each of the three per-axis canvases. Flips the
    // shared UBO's perAxisRoute selector so the shader reconstructs world-pos
    // face-locally (perAxisCellToWorld3D), then restores it to 0 so downstream
    // single-canvas passes are unaffected. The world AO band is per-axis-correct
    // because the canvas is a same-axis cardinal-iso lattice.
    void dispatchPerAxisAO(
        C_PerAxisTrixelCanvases &axes,
        const C_TriangleCanvasTextures &mainTextures,
        const C_CanvasAOTexture &mainAO
    ) {
        // perAxisRoute is a boolean route flag on the lighting path (any nonzero
        // = per-axis canvas); the shader recovers the axis per-pixel from faceId,
        // NOT from this field — distinct from stage-1's 1/2/3 = X/Y/Z axis selector.
        const int kPerAxisRoute = 1;
        voxelFrameDataBuf_
            ->subData(offsetof(FrameDataVoxelToCanvas, perAxisRoute_), sizeof(int), &kPerAxisRoute);
        // Recover world-pos with the SAME #1431-capped lattice density the store
        // wrote (perAxisCellToWorld3D reads voxelRenderOptions.y); restored below.
        IRPrefab::PerAxisCanvas::setUboSubdivisionDensity(
            voxelFrameDataBuf_,
            IRPrefab::PerAxisCanvas::subdivisionDensity()
        );
        // #2256: dispatch indirectly over only each axis's compacted occupied
        // cells (filled by the STAGE_1 per-axis compaction into these
        // component-owned buffers) instead of sweeping the full worst-case grid.
        Buffer *cellCompacted = axes.cellCompacted_.second;
        Buffer *cellIndirect = axes.cellIndirect_.second;
        const int regionStride = axes.cellRegionStride_;
        for (int axis = 0; axis < C_PerAxisTrixelCanvases::kAxisCount; ++axis) {
            auto &tex = axes.axes_[axis];
            tex.distances_.second->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
            tex.ao_.second->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
            cellCompacted->bindRange(
                BufferTarget::SHADER_STORAGE,
                kBufferIndex_PerAxisCellCompacted,
                static_cast<std::ptrdiff_t>(axis) * regionStride *
                    static_cast<int>(sizeof(std::uint32_t)),
                static_cast<size_t>(regionStride) * sizeof(std::uint32_t)
            );
            cellIndirect->bindRange(
                BufferTarget::SHADER_STORAGE,
                kBufferIndex_PerAxisCellIndirect,
                static_cast<std::ptrdiff_t>(axis) * kPerAxisCellIndirectStrideBytes,
                kPerAxisCellIndirectStrideBytes
            );
            IRRender::device()->dispatchComputeIndirect(
                cellIndirect,
                static_cast<std::ptrdiff_t>(axis) * kPerAxisCellIndirectStrideBytes +
                    kPerAxisCellDispatchArgsOffsetBytes
            );
        }
        // One barrier after the 3 independent per-axis dispatches (each axis
        // writes its own AO image texture — disjoint outputs, so dispatch order
        // doesn't matter) so they overlap on the GPU instead of serializing per
        // axis (#1311).
        IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
        const int kSingleCanvasRoute = 0;
        voxelFrameDataBuf_->subData(
            offsetof(FrameDataVoxelToCanvas, perAxisRoute_),
            sizeof(int),
            &kSingleCanvasRoute
        );
        // Restore the uncapped density for downstream single-canvas passes.
        IRPrefab::PerAxisCanvas::setUboSubdivisionDensity(
            voxelFrameDataBuf_,
            IRRender::getVoxelRenderEffectiveSubdivisions()
        );
        // Restore slots 25/26 to the voxel-compaction buffers (#1961/#2256) the
        // per-axis loop above borrowed via bindRange — VOXEL_TO_TRIXEL_STAGE_1's
        // single-canvas compact binds them once at create() and trusts sticky
        // global state thereafter, so a leaked borrow re-reads the cell list as
        // the voxel index list next frame and corrupts world voxels.
        IRPrefab::PerAxisCanvas::restoreVoxelCompactionSlots(voxelCompactedBuf_, voxelIndirectBuf_);
        // Restore the main-canvas image bindings the loop overwrote. The Metal
        // backend's image-binding table persists across frames and is read on
        // every dispatch; leaving the per-axis textures bound here would dangle
        // when release() frees them next frame at the cardinal (#1311 crash).
        mainTextures.getTextureDistances()
            ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
        mainAO.getTexture()->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
    }

    void beginTick() {
        program_->use();
        // AO only writes `aoEnabled_` into the shared FrameDataSun
        // buffer. All other fields carry the previous frame's values
        // written by BAKE_SUN_SHADOW_MAP's tick — AO must not read
        // them. BAKE_SUN_SHADOW_MAP's tick is the authoritative
        // writer for the full struct (subData(0, sizeof(FrameDataSun),
        // &frameData_) after its beginTick populates frameData_); AO
        // runs before it in pipeline order, so this partial write is
        // safe and the two systems agree on `aoEnabled_` via the same
        // IRRender::getAOEnabled() source.
        if (sunFrameDataBuf_ == nullptr) {
            sunFrameDataBuf_ = IRRender::getNamedResource<Buffer>("ComputeSunShadowFrameData");
        }
        int aoEnabledFlag = IRRender::getAOEnabled() ? 1 : 0;
        sunFrameDataBuf_->subData(offsetof(FrameDataSun, aoEnabled_), sizeof(int), &aoEnabledFlag);

        // Resolve the main canvas + its per-axis voxel canvases for the smooth-
        // yaw lighting path (#1311). Re-resolved every frame; never held across
        // frames. Null unless the component exists AND is currently allocated.
        perAxisCanvasEntity_ = IRRender::getCanvas("main");
        perAxisCanvases_ = nullptr;
        if (perAxisCanvasEntity_ != IREntity::kNullEntity) {
            auto perAxis =
                IREntity::getComponentOptional<C_PerAxisTrixelCanvases>(perAxisCanvasEntity_);
            if (perAxis.has_value()) {
                perAxisCanvases_ = perAxis.value();
            }
        }
        // Cache the main canvas's voxel-frame inputs for the endTick restore
        // (#1558).
        resolveMainCanvasVoxelFrameInputs(
            perAxisCanvasEntity_,
            &mainCanvasTextures_,
            &mainVoxelPool_,
            &mainCanvasRotation_
        );
    }

    // Restore the main world canvas's voxel frame data so BAKE_SUN_SHADOW_MAP /
    // COMPUTE_SUN_SHADOW (which run after AO and read the shared voxel UBO) see
    // the world frame even when a detached re-voxelize canvas authored its own
    // above (#1558). Byte-identical render output for single-lit-canvas scenes
    // (cullIso, the only field buildVoxelFrameData omits, is unused downstream).
    void endTick() {
        restoreMainCanvasVoxelFrame(
            scratchVoxelFrame_,
            voxelFrameDataBuf_,
            mainCanvasTextures_,
            mainVoxelPool_,
            mainCanvasRotation_
        );
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "ComputeVoxelAOProgram",
            std::vector{ShaderStage{IRRender::kFileCompComputeVoxelAO, ShaderType::COMPUTE}}
        );

        SystemId systemId = registerSystem<
            COMPUTE_VOXEL_AO,
            C_TriangleCanvasTextures,
            C_CanvasAOTexture,
            C_TrixelCanvasRenderBehavior>("ComputeVoxelAO");
        auto *p = getSystemParams<System<COMPUTE_VOXEL_AO>>(systemId);
        p->program_ = IRRender::getNamedResource<ShaderProgram>("ComputeVoxelAOProgram");
        p->voxelFrameDataBuf_ = IRRender::getNamedResource<Buffer>("SingleVoxelFrameData");
        // NOT observer-tagged: the tick owns GpuSubStageScopes (#2281), which
        // reuse the observer's timestamp attachment slot.
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_COMPUTE_VOXEL_AO_H */
