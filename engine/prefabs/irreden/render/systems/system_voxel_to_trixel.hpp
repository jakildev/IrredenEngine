#ifndef SYSTEM_VOXEL_TO_TRIXEL_H
#define SYSTEM_VOXEL_TO_TRIXEL_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/profile/scope_timer.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/common/components/component_position_2d.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/render/components/component_camera_position_2d_iso.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_triangle_canvas_background.hpp>
#include <irreden/render/cull_viewport_state.hpp>
#include <irreden/render/sun_shadow_constants.hpp>
#include <irreden/render/camera.hpp>

#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

inline ivec2 voxelDispatchGridForCount(int voxelCount) {
    constexpr int kMaxDispatchGroupsX = 1024;
    const int groupsX = IRMath::min(voxelCount, kMaxDispatchGroupsX);
    const int groupsY = IRMath::divCeil(voxelCount, groupsX);
    return ivec2(groupsX, groupsY);
}

inline const std::vector<std::uint32_t> &
buildChunkVisibilityMask(C_VoxelPool &pool, IsoBounds2D viewport) {
    static thread_local std::vector<std::uint32_t> mask;
    pool.rebuildChunkBounds();
    int chunkCount = pool.getChunkCount();
    mask.assign(chunkCount, 0);

    auto &bounds = pool.getChunkBounds();
    for (int c = 0; c < chunkCount; ++c) {
        const auto &cb = bounds[c];
        if (cb.isoMax_.x >= viewport.min_.x && cb.isoMin_.x <= viewport.max_.x &&
            cb.isoMax_.y >= viewport.min_.y && cb.isoMin_.y <= viewport.max_.y) {
            mask[c] = 1;
        }
    }
    return mask;
}

inline void buildVoxelFrameData(
    FrameDataVoxelToCanvas &frameData,
    const C_TriangleCanvasTextures &canvas,
    int liveVoxelCount,
    const C_CanvasLocalRotation &canvasRotation
) {
    const auto renderMode = IRRender::getSubdivisionMode();
    const int effectiveSubdivisions = IRRender::getVoxelRenderEffectiveSubdivisions();
    const ivec2 dispatchGrid = voxelDispatchGridForCount(liveVoxelCount);

    frameData.cameraTrixelOffset_ = IRRender::getCameraPosition2DIso();
    frameData.trixelCanvasOffsetZ1_ = IRMath::trixelOriginOffsetZ1(canvas.size_);
    frameData.voxelRenderOptions_ = ivec2(static_cast<int>(renderMode), effectiveSubdivisions);
    frameData.voxelDispatchGrid_ = dispatchGrid;
    frameData.voxelCount_ = liveVoxelCount;
    frameData.canvasSizePixels_ = canvas.size_;

    // A non-zero `canvasRotation` marks a detached entity canvas (the main
    // world canvas keeps the all-zero `C_CanvasLocalRotation::kSentinelNoRotation`
    // sentinel). A detached canvas rasterizes its voxels in the entity's own
    // model space — camera yaw zeroed — and `faceDeform_` carries the full SO(3)
    // per-face deformation for the entity's rotation (T-295).
    const bool detachedCanvas = canvasRotation.isDetached();
    frameData.isDetachedCanvas_ = detachedCanvas ? 1.0f : 0.0f;
    if (detachedCanvas) {
        frameData.visualYaw_ = 0.0f;
        frameData.rasterYaw_ = 0.0f;
        frameData.residualYaw_ = 0.0f;
        // Snap to the nearest of the 24 cube orientations and deform by the
        // residual only: a cube is invariant under the snap, so this keeps
        // the per-face skew small enough to stay clean (T-295).
        const vec4 residual = IRMath::octahedralSnapResidual(canvasRotation.rotation_);
        const mat2 fdX = IRMath::faceDeformationMatrixSO3(IRMath::kXFace, residual);
        const mat2 fdY = IRMath::faceDeformationMatrixSO3(IRMath::kYFace, residual);
        const mat2 fdZ = IRMath::faceDeformationMatrixSO3(IRMath::kZFace, residual);
        frameData.faceDeform_[IRMath::kXFace] = vec4(fdX[0], fdX[1]);
        frameData.faceDeform_[IRMath::kYFace] = vec4(fdY[0], fdY[1]);
        frameData.faceDeform_[IRMath::kZFace] = vec4(fdZ[0], fdZ[1]);
        return;
    }

    // Main world canvas: rasterYaw picks the integer trixel basis permutation
    // (T-055); residualYaw is folded into faceDeform_[] which the trixel emit
    // shader applies to each sub-pixel offset in 2D iso space (T-293, replaces
    // the T-058 / T-322 screen-space bilinear residual composite).
    frameData.visualYaw_ = IRPrefab::Camera::getYaw();
    const auto [rasterYaw, residualYaw] = IRPrefab::Camera::computeYawSplit(frameData.visualYaw_);
    frameData.rasterYaw_ = rasterYaw;
    frameData.residualYaw_ = residualYaw;
    const mat2 fdX = IRMath::faceDeformationMatrix(IRMath::kXFace, residualYaw);
    const mat2 fdY = IRMath::faceDeformationMatrix(IRMath::kYFace, residualYaw);
    const mat2 fdZ = IRMath::faceDeformationMatrix(IRMath::kZFace, residualYaw);
    frameData.faceDeform_[IRMath::kXFace] = vec4(fdX[0], fdX[1]);
    frameData.faceDeform_[IRMath::kYFace] = vec4(fdY[0], fdY[1]);
    frameData.faceDeform_[IRMath::kZFace] = vec4(fdZ[0], fdZ[1]);
}

inline void
clearCanvasAndDistances(IREntity::EntityId canvasEntity, C_TriangleCanvasTextures &canvas) {
    auto background = IREntity::getComponentOptional<C_TriangleCanvasBackground>(canvasEntity);
    if (background.has_value()) {
        (*background.value()).clearCanvasWithBackground(canvas);
    } else {
        canvas.clear();
    }
    static const std::int32_t clearValue =
        static_cast<std::int32_t>(IRConstants::kTrixelDistanceMaxDistance);
    IRRender::device()->clearTexImage(canvas.getTextureDistances(), 0, &clearValue);
}

inline void syncEntityIds(C_VoxelPool &pool, int liveCount, Buffer *entityIdBuf) {
    if (!pool.isEntityIdsDirty()) {
        return;
    }
    entityIdBuf->subData(0, liveCount * sizeof(IREntity::EntityId), pool.getEntityIds().data());
    pool.clearEntityIdsDirty();
}

// Drain `pool.getPendingPositionRanges()` to the position SSBO as one
// `subData` per contiguous run. Mirrors `C_GPUParticlePool::flushPendingSpawns`
// — sort, coalesce, emit. Empty list is a fast-path no-op so a static
// voxel scene pays zero bytes/frame.
inline void flushPendingPositionRanges(C_VoxelPool &pool, Buffer *buf) {
    auto &ranges = pool.getPendingPositionRanges();
    if (ranges.empty()) {
        return;
    }
    constexpr size_t kStride = sizeof(C_PositionGlobal3D);
    const auto &globals = pool.getPositionGlobals();

    // A saturated queue no longer represents a small moved subset — the
    // ranges cover most of the live buffer (every moving voxel set
    // re-queued across N catch-up update ticks). Sorting + coalescing
    // thousands of fragments costs more than one whole-live-range
    // upload, so take that path. See C_VoxelPool::kMaxPendingPositionRanges.
    if (ranges.size() >= C_VoxelPool::kMaxPendingPositionRanges) {
        const int liveCount = pool.getLiveVoxelCount();
        if (liveCount > 0) {
            buf->subData(0, static_cast<size_t>(liveCount) * kStride, globals.data());
        }
        pool.clearPendingPositionRanges();
        return;
    }

    std::sort(ranges.begin(), ranges.end());
    size_t runStart = ranges.front().first;
    size_t runEnd = runStart + ranges.front().second;
    for (size_t i = 1; i < ranges.size(); ++i) {
        const size_t s = ranges[i].first;
        const size_t e = s + ranges[i].second;
        if (s <= runEnd) {
            // Overlapping or contiguous with the open run — extend.
            if (e > runEnd) {
                runEnd = e;
            }
            continue;
        }
        IR_ASSERT(
            runEnd <= globals.size(),
            "flushPendingPositionRanges: runEnd {} exceeds globals.size() {}",
            runEnd,
            globals.size()
        );
        buf->subData(
            static_cast<std::ptrdiff_t>(runStart * kStride),
            (runEnd - runStart) * kStride,
            &globals[runStart]
        );
        runStart = s;
        runEnd = e;
    }
    IR_ASSERT(
        runEnd <= globals.size(),
        "flushPendingPositionRanges: runEnd {} exceeds globals.size() {}",
        runEnd,
        globals.size()
    );
    buf->subData(
        static_cast<std::ptrdiff_t>(runStart * kStride),
        (runEnd - runStart) * kStride,
        &globals[runStart]
    );

    pool.clearPendingPositionRanges();
}

template <> struct System<VOXEL_TO_TRIXEL_STAGE_1> {
    ShaderProgram *compactProgram_ = nullptr;
    ShaderProgram *stage1Program_ = nullptr;
    ShaderProgram *stage2Program_ = nullptr;
    Buffer *frameDataBuf_ = nullptr;
    Buffer *voxelPosBuf_ = nullptr;
    Buffer *voxelColorBuf_ = nullptr;
    Buffer *voxelActiveMaskBuf_ = nullptr;
    Buffer *voxelEntityIdBuf_ = nullptr;
    Buffer *chunkVisBuf_ = nullptr;
    Buffer *indirectBuf_ = nullptr;
    FrameDataVoxelToCanvas frameData_{};
    // Resolved once per frame in beginTick; read by the per-entity tick.
    vec3 sunDir_{};
    float sweepDistance_ = 0.0f;
    // Log-throttle state — emit the render-mode log line only when
    // mode or effective subdivisions change.
    int previousRenderMode_ = -1;
    int previousEffectiveSubdivisions_ = -1;
    // Last canvas whose position SSBO contents were written to
    // `voxelPosBuf_`. Positions are otherwise pushed at mutation time by
    // `UPDATE_VOXEL_SET_CHILDREN`; we still need a per-canvas full
    // re-seed whenever the buffer's last-writer was a different canvas
    // (multi-canvas-with-voxels scenes share one bind point, so the
    // mutator pushes from a foreign canvas would have overwritten the
    // slots this dispatch is about to read). For the steady single-
    // canvas case the tracker is set on the first tick and never
    // re-fires.
    // NOTE: with N detached canvases every canvas triggers a mismatch on
    // the other N-1 ticks, producing N full position SSBO re-seeds per
    // frame. The pending-range coalescing optimization (used for the
    // single-canvas path) is bypassed for all but the last-ticked canvas.
    // Profile this path if the scene voxel count grows significantly.
    IREntity::EntityId lastUploadedCanvas_ = IREntity::kNullEntity;

    void tick(
        IREntity::EntityId entity,
        C_VoxelPool &voxelPool,
        C_TriangleCanvasTextures &triangleCanvasTextures,
        const C_CanvasLocalRotation &canvasLocalRotation
    ) {
        const int liveVoxelCount = voxelPool.getLiveVoxelCount();

        // Per-frame canvas clear runs unconditionally — every downstream
        // writer (shapes, stateless/GPU particles, text overlays, lighting)
        // assumes the canvas color + distance + entity-id textures start
        // each frame at their clear values. Gating the clear on
        // `liveVoxelCount > 0` smeared frame-over-frame content for any
        // canvas with a voxel pool but zero live voxels (e.g. the
        // stateless-particles demo, any pure-shape or pure-particle scene).
        // Keep this above the early-return so an empty voxel pool no
        // longer dictates whether the canvas refreshes.
        {
            IR_PROFILE_SCOPE("vs1_clear");
            clearCanvasAndDistances(entity, triangleCanvasTextures);
        }

        if (liveVoxelCount == 0)
            return;

        IRRender::updateCullViewport(
            IRRender::getCameraPosition2DIso(),
            IRRender::getCameraZoom(),
            triangleCanvasTextures.size_
        );
        const auto &cull = IRRender::getCullViewport();

        buildVoxelFrameData(
            frameData_,
            triangleCanvasTextures,
            liveVoxelCount,
            canvasLocalRotation
        );

        const int renderMode = frameData_.voxelRenderOptions_.x;
        const int effectiveSub = frameData_.voxelRenderOptions_.y;
        if (renderMode != previousRenderMode_ || effectiveSub != previousEffectiveSubdivisions_) {
            const vec2 zoom = IRRender::getCameraZoom();
            IRE_LOG_INFO(
                "Voxel render mode={}, base_subdivisions={}, zoom_scale={}, "
                "effective_subdivisions={}",
                renderMode,
                IRRender::getVoxelRenderSubdivisions(),
                static_cast<int>(IRMath::round(IRMath::max(zoom.x, zoom.y))),
                effectiveSub
            );
            previousRenderMode_ = renderMode;
            previousEffectiveSubdivisions_ = effectiveSub;
        }

        // Sun direction and sweep distance are resolved once per
        // frame in beginTick (via IRPrefab::SunShadow::getFrameSunDirection)
        // and cached in params, so no C_LightSource archetype scan
        // runs per entity.
        const float sweepDistance = sweepDistance_;

        constexpr int kChunkMargin = 8;
        const IsoBounds2D chunkVp =
            IRMath::shadowFeederIsoBounds(cull.isoViewport(kChunkMargin), sunDir_, sweepDistance);
        const auto &uploadMask = buildChunkVisibilityMask(voxelPool, chunkVp);
        chunkVisBuf_->subData(0, uploadMask.size() * sizeof(std::uint32_t), uploadMask.data());

        constexpr int kGpuMargin = 4;
        const IsoBounds2D gpuVp =
            IRMath::shadowFeederIsoBounds(cull.isoViewport(kGpuMargin), sunDir_, sweepDistance);
        frameData_.cullIsoMin_ = ivec2(IRMath::floor(gpuVp.min_));
        frameData_.cullIsoMax_ = ivec2(IRMath::ceil(gpuVp.max_));
        frameDataBuf_->subData(0, sizeof(FrameDataVoxelToCanvas), &frameData_);

        // Voxel positions are now uploaded via the pending-range queue
        // populated by `UPDATE_VOXEL_SET_CHILDREN` (`cpp-ecs.md`
        // pending-list-flush rule). Single-canvas-with-voxels steady-
        // state: queue empty for static entities → zero `subData`
        // bytes/frame; moving entities coalesce into runs. Multi-
        // canvas-with-voxels: each canvas's SSBO state can be clobbered
        // by another canvas's mutator pushes since both write to the
        // shared `voxelPosBuf_`, so on a canvas switch we re-seed the
        // whole live range and drain this pool's queue.
        {
            IR_PROFILE_SCOPE("vs1_pos");
            if (lastUploadedCanvas_ != entity) {
                voxelPosBuf_->subData(
                    0,
                    liveVoxelCount * sizeof(C_PositionGlobal3D),
                    voxelPool.getPositionGlobals().data()
                );
                voxelPool.clearPendingPositionRanges();
                lastUploadedCanvas_ = entity;
            } else {
                flushPendingPositionRanges(voxelPool, voxelPosBuf_);
            }
        }
        voxelColorBuf_->subData(0, liveVoxelCount * sizeof(C_Voxel), voxelPool.getColors().data());
        // Active-mask covers the live prefix; upload the matching whole-word
        // window (`divCeil(liveVoxelCount, kVoxelActiveMaskBits)` words) so
        // the GPU compact shader sees an up-to-date mirror of CPU alpha.
        const std::size_t activeMaskWords = IRMath::divCeil(
            static_cast<int>(liveVoxelCount),
            static_cast<int>(kVoxelActiveMaskBits)
        );
        if (activeMaskWords > 0) {
            voxelActiveMaskBuf_->subData(
                0,
                activeMaskWords * sizeof(std::uint32_t),
                voxelPool.getActiveMask().data()
            );
        }
        syncEntityIds(voxelPool, liveVoxelCount, voxelEntityIdBuf_);

        // Cull diagnostic readback (gated by gpu_stage_timing.enabled_).
        // The buffer still holds the prior frame's visibleCount; reading
        // it here — before we zero it for this frame's compact pass —
        // requires no explicit fence: the driver serializes the CPU read
        // against the prior frame's already-retired compact write.
        // Frame N+1 reads frame N's value; the first frame reports 0,
        // contributing a 1/N bias to the running average — negligible
        // over typical measurement runs.
        if (gpuStageTiming().enabled_) {
            VoxelIndirectDispatchParams previous{};
            indirectBuf_->getSubData(0, sizeof(VoxelIndirectDispatchParams), &previous);
            gpuStageTiming().visibleVoxelCount_ = previous.visibleCount;
            gpuStageTiming().totalVoxelCount_ = static_cast<std::uint32_t>(liveVoxelCount);
            voxelCullAccumulator().record(
                previous.visibleCount,
                static_cast<std::uint32_t>(liveVoxelCount)
            );
        }

        const VoxelIndirectDispatchParams zeroed{};
        indirectBuf_->subData(0, sizeof(VoxelIndirectDispatchParams), &zeroed);

        compactProgram_->use();
        constexpr int kCompactLocalSize = 64;
        const int compactGroups = IRMath::divCeil(liveVoxelCount, kCompactLocalSize);
        const ivec2 compactGrid = voxelDispatchGridForCount(compactGroups);
        IRRender::device()->dispatchCompute(compactGrid.x, compactGrid.y, 1);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
        IRRender::device()->memoryBarrier(BarrierType::COMMAND);

        stage1Program_->use();
        triangleCanvasTextures.getTextureDistances()
            ->bindAsImage(1, TextureAccess::READ_ONLY, TextureFormat::R32I);
        IRRender::device()->dispatchComputeIndirect(indirectBuf_, 0);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);

        // Stage 2 runs in the SAME per-canvas tick rather than as a separate
        // system. The compact + position/color SSBOs (`voxelPosBuf_`,
        // `voxelColorBuf_`, `CompactedVoxelIndices`, `IndirectDispatchParams`)
        // are single-instance, shared across every voxel-pool canvas. A
        // separate STAGE_2 system runs after STAGE_1 has ticked *all*
        // canvases, so those buffers only ever hold the last canvas's data —
        // every detached canvas would rasterize the wrong voxels. Folding the
        // stage-2 dispatch in here keeps each canvas's upload→compact→stage1
        // →stage2 sequence atomic before the next canvas overwrites the
        // buffers.
        stage2Program_->use();
        triangleCanvasTextures.getTextureColors()
            ->bindAsImage(0, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
        triangleCanvasTextures.getTextureDistances()
            ->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::R32I);
        triangleCanvasTextures.getTextureEntityIds()
            ->bindAsImage(2, TextureAccess::WRITE_ONLY, TextureFormat::RG32UI);
        IRRender::device()->dispatchComputeIndirect(indirectBuf_, 0);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
    }

    void beginTick() {
        // Resolve sun direction once per frame so the per-entity tick
        // reads the cached value instead of scanning C_LightSource
        // once per voxel-pool-canvas pair.
        const bool shadowsEnabled = IRRender::getSunShadowsEnabled();
        sunDir_ = shadowsEnabled ? IRPrefab::SunShadow::getFrameSunDirection() : vec3(0.0f);
        sweepDistance_ = shadowsEnabled ? IRPrefab::SunShadow::kSunShadowMaxDistance : 0.0f;

        IREntity::EntityId backgroundCanvas = IRRender::getCanvas("background");
        auto background =
            IREntity::getComponentOptional<C_TriangleCanvasBackground>(backgroundCanvas);
        auto backgroundTextures =
            IREntity::getComponentOptional<C_TriangleCanvasTextures>(backgroundCanvas);
        if (background.has_value() && backgroundTextures.has_value()) {
            (*background.value()).clearCanvasWithBackground(*backgroundTextures.value());
        }
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "VoxelCompactProgram",
            std::vector{ShaderStage{IRRender::kFileCompVoxelVisibilityCompact, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<ShaderProgram>(
            "SingleVoxelProgram1",
            std::vector{ShaderStage{IRRender::kFileCompVoxelToTrixelStage1, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<ShaderProgram>(
            "SingleVoxel2",
            std::vector{ShaderStage{IRRender::kFileCompVoxelToTrixelStage2, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<Buffer>(
            "SingleVoxelFrameData",
            nullptr,
            sizeof(FrameDataVoxelToCanvas),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_FrameDataVoxelToCanvas
        );
        const int maxSingleVoxels = IRRender::VoxelPoolConfig::getTotalSize();
        IRRender::createNamedResource<Buffer>(
            "VoxelPositionBuffer",
            nullptr,
            maxSingleVoxels * sizeof(C_Position3D),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_SingleVoxelPositions
        );
        IRRender::createNamedResource<Buffer>(
            "VoxelColorBuffer",
            nullptr,
            maxSingleVoxels * sizeof(C_Voxel),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_SingleVoxelColors
        );
        const int maxActiveMaskWords =
            IRMath::divCeil(maxSingleVoxels, static_cast<int>(kVoxelActiveMaskBits));
        IRRender::createNamedResource<Buffer>(
            "VoxelActiveMaskBuffer",
            nullptr,
            maxActiveMaskWords * sizeof(std::uint32_t),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_VoxelActiveMask
        );
        IRRender::createNamedResource<Buffer>(
            "VoxelEntityIdBuffer",
            nullptr,
            maxSingleVoxels * sizeof(IREntity::EntityId),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_VoxelEntityIds
        );
        const int maxVoxelPoolChunks = IRMath::divCeil(maxSingleVoxels, IRRender::kVoxelChunkSize);
        IRRender::createNamedResource<Buffer>(
            "ChunkVisibilityBuffer",
            nullptr,
            maxVoxelPoolChunks * sizeof(std::uint32_t),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_ChunkVisibility
        );
        IRRender::createNamedResource<Buffer>(
            "CompactedVoxelIndices",
            nullptr,
            maxSingleVoxels * sizeof(std::uint32_t),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_CompactedVoxelIndices
        );
        IRRender::createNamedResource<Buffer>(
            "IndirectDispatchParams",
            nullptr,
            sizeof(VoxelIndirectDispatchParams),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_IndirectDispatchParams
        );

        SystemId systemId = registerSystem<
            VOXEL_TO_TRIXEL_STAGE_1,
            C_VoxelPool,
            C_TriangleCanvasTextures,
            C_CanvasLocalRotation>("SingleVoxelToCanvasFirst");
        auto *p = getSystemParams<System<VOXEL_TO_TRIXEL_STAGE_1>>(systemId);
        p->compactProgram_ = IRRender::getNamedResource<ShaderProgram>("VoxelCompactProgram");
        p->stage1Program_ = IRRender::getNamedResource<ShaderProgram>("SingleVoxelProgram1");
        p->stage2Program_ = IRRender::getNamedResource<ShaderProgram>("SingleVoxel2");
        p->frameDataBuf_ = IRRender::getNamedResource<Buffer>("SingleVoxelFrameData");
        p->voxelPosBuf_ = IRRender::getNamedResource<Buffer>("VoxelPositionBuffer");
        p->voxelColorBuf_ = IRRender::getNamedResource<Buffer>("VoxelColorBuffer");
        p->voxelActiveMaskBuf_ = IRRender::getNamedResource<Buffer>("VoxelActiveMaskBuffer");
        p->voxelEntityIdBuf_ = IRRender::getNamedResource<Buffer>("VoxelEntityIdBuffer");
        p->chunkVisBuf_ = IRRender::getNamedResource<Buffer>("ChunkVisibilityBuffer");
        p->indirectBuf_ = IRRender::getNamedResource<Buffer>("IndirectDispatchParams");
        // The observer-based timing brackets the entire system tick. The
        // formerly-separate canvasClear and voxelCompact sub-stages now
        // collapse into voxelStage1's measurement; their registry slots
        // remain at 0.0f for API-compatibility.
        IRRender::tagGpuStage(systemId, "voxelStage1");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_VOXEL_TO_TRIXEL_H */
