#ifndef SYSTEM_VOXEL_TO_TRIXEL_H
#define SYSTEM_VOXEL_TO_TRIXEL_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/profile/scope_timer.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/common/components/component_position_2d.hpp>
#include <irreden/render/components/component_camera_position_2d_iso.hpp>
#include <irreden/render/voxel_pool_allocation.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_triangle_canvas_background.hpp>
#include <irreden/render/cull_viewport_state.hpp>
#include <irreden/render/sun_shadow_constants.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/per_axis_canvas.hpp>
#include <irreden/render/detached_revoxelize.hpp>
#include <irreden/render/voxel_dispatch_grid.hpp>
#include <irreden/render/voxel_frame_data.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/components/component_detached_revoxelize_buffer.hpp>

#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <utility>
#include <vector>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

inline const std::vector<std::uint32_t> &buildChunkVisibilityMask(
    C_VoxelPool &pool,
    IsoBounds2D viewport,
    CardinalIndex cardinalIndex = CardinalIndex::k0,
    bool useContinuousYaw = false,
    float visualYaw = 0.0f
) {
    static thread_local std::vector<std::uint32_t> mask;
    pool.rebuildChunkBounds(cardinalIndex, useContinuousYaw, visualYaw);
    int chunkCount = pool.getChunkCount();
    mask.assign(chunkCount, 0);

    auto &bounds = pool.getChunkBounds();
    for (int c = 0; c < chunkCount; ++c) {
        const auto &cb = bounds[c];
        if (viewport.overlapsAABB(cb.isoMin_, cb.isoMax_)) {
            mask[c] = 1;
        }
    }
    return mask;
}

// `buildVoxelFrameData` now lives in `<irreden/render/voxel_frame_data.hpp>`
// (shared with COMPUTE_VOXEL_AO + LIGHTING_TO_TRIXEL, which re-author the
// iterating canvas's frame data per dispatch for re-voxelize P4 / #1558).

inline void
clearCanvasAndDistances(IREntity::EntityId canvasEntity, C_TriangleCanvasTextures &canvas) {
    auto background = IREntity::getComponentOptional<C_TriangleCanvasBackground>(canvasEntity);
    if (background.has_value()) {
        (*background.value()).clearCanvasWithBackground(canvas);
    } else {
        canvas.clear();
    }
    // Distance buffer must be reset every frame regardless of whether the
    // background updated the color canvas. clearCanvasWithBackground may
    // skip on throttled frames (kPulsePattern) or no-op (kGradient), but
    // the voxel-to-trixel pass writes per-pixel distances each frame.
    // Use device-level clearTexImage rather than Texture2D::clear —
    // on Metal the Texture2D::clear path fails to clear R32I textures
    // reliably, leaving stale distances that cull visible voxels.
    static constexpr std::int32_t kDistanceClear =
        static_cast<std::int32_t>(IRConstants::kTrixelDistanceMaxDistance);
    IRRender::device()->clearTexImage(canvas.getTextureDistances(), 0, &kDistanceClear);
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
    constexpr size_t kStride = sizeof(IRRender::VoxelGpuPosition);
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

// Re-seed binding 5 on a canvas switch without clobbering GPU-prepass output.
// Scans `pool.getTransformIndices()` up to `liveCount` and emits one `subData`
// per contiguous static run. GPU-transformed slots (index != kVoxelTransformStatic)
// are skipped — UPDATE_VOXEL_POSITIONS_GPU already wrote their correct world
// positions ahead of this stage. When all voxels are static this produces a
// single upload equivalent to the old full-range subData.
inline void flushStaticPositionRanges(C_VoxelPool &pool, Buffer *buf, int liveCount) {
    constexpr size_t kStride = sizeof(IRRender::VoxelGpuPosition);
    const auto &globals = pool.getPositionGlobals();
    const auto &indices = pool.getTransformIndices();
    const int n = IRMath::min(liveCount, static_cast<int>(indices.size()));

    int runStart = -1;
    for (int i = 0; i <= n; ++i) {
        const bool isStatic = (i < n) && (indices[i] == IRRender::kVoxelTransformStatic);
        if (isStatic) {
            if (runStart < 0)
                runStart = i;
        } else if (runStart >= 0) {
            buf->subData(
                static_cast<std::ptrdiff_t>(runStart) * kStride,
                static_cast<size_t>(i - runStart) * kStride,
                &globals[runStart]
            );
            runStart = -1;
        }
    }
}

template <> struct System<VOXEL_TO_TRIXEL_STAGE_1> {
    ShaderProgram *compactProgram_ = nullptr;
    ShaderProgram *stage1Program_ = nullptr;
    ShaderProgram *stage2Program_ = nullptr;
    // Detached re-voxelize GPU scatter (#1556): fills binding 5 for a
    // DETACHED_REVOXELIZE pool from its resident locals + the canvas quat, in
    // place of the CPU flushStaticPositionRanges.
    ShaderProgram *revoxelizeProgram_ = nullptr;
    Buffer *revoxelizeParamsBuf_ = nullptr;
    Buffer *frameDataBuf_ = nullptr;
    Buffer *voxelPosBuf_ = nullptr;
    Buffer *voxelColorBuf_ = nullptr;
    Buffer *voxelActiveMaskBuf_ = nullptr;
    Buffer *voxelEntityIdBuf_ = nullptr;
    Buffer *chunkVisBuf_ = nullptr;
    Buffer *indirectBuf_ = nullptr;
    FrameDataVoxelToCanvas frameData_{};
    // Resolved once per frame in beginTick; read by the per-entity tick.
    IRPrefab::SunShadow::ShadowFeederParams shadowFeederParams_{};
    // Log-throttle state — emit the render-mode log line only when
    // mode or effective subdivisions change.
    int previousRenderMode_ = -1;
    int previousEffectiveSubdivisions_ = -1;
    // One-shot per-axis subdivision-cap warn throttle (#1431). Latches the last
    // (effSub, capped) pair logged so the "cap engaged, sub-voxel detail lost
    // while rotating" warning fires once per distinct cap transition rather than
    // every frame — mirrors the light-volume out-of-range one-shot warn.
    int previousCapWarnEffSub_ = -1;
    int previousCapWarnDensity_ = -1;
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

    // Re-resolved every frame in beginTick — never held across frames. (.claude/rules/cpp-ecs.md)
    IREntity::EntityId perAxisCanvasEntity_ = IREntity::kNullEntity;
    C_PerAxisTrixelCanvases *perAxisCanvases_ = nullptr;

    // Every DETACHED_REVOXELIZE canvas's resident locals buffer, resolved once
    // per frame in beginTick by IRPrefab::DetachedRevoxelize::syncResidentBuffers
    // and consumed by the per-entity tick (#1556). Keyed by canvas entity +
    // linear-scanned in the tick because the iterated canvas isn't in the system's
    // template params, and a re-voxelize canvas's C_DetachedRevoxelizeBuffer can't
    // be added to the archetype filter without dropping every other voxel-pool
    // canvas. The pointers are column addresses valid only within this frame's
    // ticks.
    std::vector<std::pair<IREntity::EntityId, C_DetachedRevoxelizeBuffer *>>
        detachedRevoxelizeBuffers_;

    // Zero / all-ones scratch for the inverse-resample re-voxelize path (#1619),
    // grown to a high-water mark and reused (cpp-ecs.md "no allocation in hot
    // ticks"). activeMaskClearScratch_ pre-clears the active-mask window the GPU
    // fill atomic-ORs onto; allVisibleChunkScratch_ marks every dest-slot chunk
    // visible (the model-space re-voxelize canvas shows the whole solid).
    std::vector<std::uint32_t> activeMaskClearScratch_;
    std::vector<std::uint32_t> allVisibleChunkScratch_;
    // #1619 step-0 diagnostic budget (remove when #1619 closes) — see the
    // readback block after the compact dispatch in tick().
    int revoxDebugBudget_ = 4;

    C_DetachedRevoxelizeBuffer *lookupDetachedRevoxelizeBuffer(IREntity::EntityId entity) const {
        for (const auto &[id, buffer] : detachedRevoxelizeBuffers_) {
            if (id == entity) {
                return buffer;
            }
        }
        return nullptr;
    }

    // Fill the GPU buffers for a DETACHED_REVOXELIZE pool, replacing the CPU
    // flushStaticPositionRanges. Two modes (see RevoxelizeDetachedParams):
    //   IDENTITY / source path (#1556) — one thread per live voxel rotates+rounds
    //     its resident local into binding 5; the CPU still uploads color + active.
    //   INVERSE resample (#1619, rotating) — one thread per DEST cell of the
    //     rotated-AABB cube inverse-looks-up the source grid and authors
    //     position + color + active for occupied dest slots (hole-free). The
    //     active-mask window is pre-cleared here so the fill's atomic-OR starts
    //     from zero; the CPU color/active uploads are skipped by the caller.
    // The SHADER_STORAGE barrier makes the writes visible to the compact +
    // stage-1 reads later in this tick. Returns the dispatch domain size (D dest
    // cells when inverse, else the live source count) so the caller drives the
    // shared compact + frame `voxelCount` from the right slot range.
    int dispatchReVoxelize(
        C_DetachedRevoxelizeBuffer &buffer,
        const C_CanvasLocalRotation &canvasRotation,
        int liveVoxelCount,
        bool isInverse
    ) {
        RevoxelizeDetachedParams params{};
        params.canvasRotation_ = canvasRotation.rotation_;
        constexpr int kLocalSize = 64;

        if (!isInverse) {
            params.dest_ = ivec4(liveVoxelCount, 0, 0, 0);
            revoxelizeParamsBuf_->subData(0, sizeof(RevoxelizeDetachedParams), &params);

            revoxelizeProgram_->use();
            voxelPosBuf_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_SingleVoxelPositions);
            buffer.residentLocals_.second->bindBase(
                BufferTarget::SHADER_STORAGE,
                kBufferIndex_LocalVoxelPositions
            );
            revoxelizeParamsBuf_->bindBase(
                BufferTarget::UNIFORM,
                kBufferIndex_RevoxelizeDetachedParams
            );

            const ivec2 grid =
                voxelDispatchGridForCount(IRMath::divCeil(liveVoxelCount, kLocalSize));
            IRRender::device()->dispatchCompute(grid.x, grid.y, 1);
            IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
            return liveVoxelCount;
        }

        const int destCount = buffer.destCount_;
        params.dest_ = ivec4(destCount, buffer.destSide_, buffer.destCenter_, 1);
        params.srcGridMin_ = ivec4(buffer.sourceGridMin_, 0);
        params.srcGridDims_ = ivec4(buffer.sourceGridDims_, 0);
        revoxelizeParamsBuf_->subData(0, sizeof(RevoxelizeDetachedParams), &params);

        // Pre-clear the active-mask window the fill atomic-ORs onto (empty dest
        // cells must read back inactive). The upload precedes the dispatch in the
        // command stream, so the clear lands before the fill's atomic writes.
        const int activeWords = IRMath::divCeil(destCount, static_cast<int>(kVoxelActiveMaskBits));
        if (static_cast<int>(activeMaskClearScratch_.size()) < activeWords) {
            activeMaskClearScratch_.resize(activeWords, 0u);
        }
        voxelActiveMaskBuf_->subData(
            0,
            static_cast<std::size_t>(activeWords) * sizeof(std::uint32_t),
            activeMaskClearScratch_.data()
        );

        revoxelizeProgram_->use();
        voxelPosBuf_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_SingleVoxelPositions);
        voxelColorBuf_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_SingleVoxelColors);
        voxelActiveMaskBuf_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_VoxelActiveMask);
        buffer.sourceGrid_.second->bindBase(
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_RevoxelizeSourceGrid
        );
        revoxelizeParamsBuf_->bindBase(
            BufferTarget::UNIFORM,
            kBufferIndex_RevoxelizeDetachedParams
        );

        const ivec2 grid = voxelDispatchGridForCount(IRMath::divCeil(destCount, kLocalSize));
        IRRender::device()->dispatchCompute(grid.x, grid.y, 1);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
        return destCount;
    }

    // Route the visible voxel faces into the three per-axis trixel canvases for
    // smooth camera Z-yaw (T2 / #1309). Runs ONLY on the main world canvas and
    // ONLY while rotating (the per-axis textures are allocated). Reuses this
    // frame's compacted-voxel list + indirect dispatch params (the single-
    // canvas pass already populated them), re-binding each axis canvas's
    // textures and flipping `perAxisRoute_` so the shaders route + continuously
    // reposition that axis's face. The single canvas the framebuffer still
    // reads is untouched, so the rendered frame stays byte-identical until T3
    // (#1310) composites these canvases. Restores the UBO to the main-canvas
    // frame data on exit so downstream stages (AO, lighting, fog) are
    // unaffected.
    void dispatchPerAxisCanvases(C_PerAxisTrixelCanvases &axes) {
        IR_PROFILE_SCOPE("vs1_per_axis");
        static constexpr std::int32_t kDistanceClear =
            static_cast<std::int32_t>(IRConstants::kTrixelDistanceMaxDistance);
        const u8vec4 kColorClear{0, 0, 0, 0};
        const uvec2 kEntityIdClear{0u, 0u};

        // Preserve the main canvas's frame-data fields the per-axis pass
        // overwrites, so the buffer is restored byte-for-byte on exit.
        const ivec2 mainOffsetZ1 = frameData_.trixelCanvasOffsetZ1_;
        const ivec2 mainCanvasSize = frameData_.canvasSizePixels_;
        const int uncappedSub = frameData_.voxelRenderOptions_.y;

        // Cap the per-axis lattice density so face-local cells stay inside the
        // bounded per-axis canvas (#1431). The canvas isn't sized for the
        // ×subPerAxis lattice, so a large effSub (high voxel_render_subdivisions
        // or zoom) overflows it and on-screen faces are silently clipped to
        // background. The capped value rides in voxelRenderOptions_.y so the
        // store shader (subPerAxis + trixelFrameOffset), the per-axis AO/lighting
        // world recovery, and the framebuffer scatter all share one consistent
        // world↔cell scale. Restored to uncappedSub on exit (cardinal /
        // single-canvas / detached paths are unaffected).
        const int cappedSub = IRPrefab::PerAxisCanvas::subdivisionDensity();
        frameData_.voxelRenderOptions_.y = cappedSub;
        if (cappedSub < uncappedSub &&
            (uncappedSub != previousCapWarnEffSub_ || cappedSub != previousCapWarnDensity_)) {
            IRE_LOG_INFO(
                "Per-axis Z-yaw store: subdivision density capped {} -> {} so "
                "on-screen voxel faces aren't clipped by the bounded per-axis "
                "canvas (#1431); sub-voxel detail reduced while rotating only.",
                uncappedSub,
                cappedSub
            );
            previousCapWarnEffSub_ = uncappedSub;
            previousCapWarnDensity_ = cappedSub;
        }

        const ivec2 perAxisOffsetZ1 = IRMath::trixelOriginOffsetZ1(axes.size_);
        for (int axis = 0; axis < C_PerAxisTrixelCanvases::kAxisCount; ++axis) {
            auto &tex = axes.axes_[axis];
            Texture2D *colors = tex.colors_.second;
            Texture2D *distances = tex.distances_.second;
            Texture2D *entityIds = tex.entityIds_.second;

            // Bind the distance image first so its Metal atomic-scratch buffer
            // exists before clearTexImage mirrors the clear value into it (a
            // freshly allocated scratch is zero-initialised, which would
            // otherwise reject every depth-matched color write on the first
            // rotating frame).
            distances->bindAsImage(1, TextureAccess::READ_ONLY, TextureFormat::R32I);
            IRRender::device()->clearTexImage(distances, 0, &kDistanceClear);
            colors->clear(PixelDataFormat::RGBA, PixelDataType::UNSIGNED_BYTE, &kColorClear[0]);
            entityIds->clear(PixelDataFormat::RG_INTEGER, PixelDataType::UINT32, &kEntityIdClear);

            frameData_.perAxisRoute_ = axis + 1;
            frameData_.trixelCanvasOffsetZ1_ = perAxisOffsetZ1;
            frameData_.canvasSizePixels_ = axes.size_;
            frameDataBuf_->subData(0, sizeof(FrameDataVoxelToCanvas), &frameData_);

            stage1Program_->use();
            distances->bindAsImage(1, TextureAccess::READ_ONLY, TextureFormat::R32I);
            IRRender::device()->dispatchComputeIndirect(indirectBuf_, 0);
            IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);

            stage2Program_->use();
            colors->bindAsImage(0, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
            distances->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::R32I);
            entityIds->bindAsImage(2, TextureAccess::WRITE_ONLY, TextureFormat::RG32UI);
            IRRender::device()->dispatchComputeIndirect(indirectBuf_, 0);
            IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
        }

        // Restore the main-canvas frame data so downstream stages read the
        // exact UBO state the single-canvas pass left.
        frameData_.perAxisRoute_ = 0;
        frameData_.trixelCanvasOffsetZ1_ = mainOffsetZ1;
        frameData_.canvasSizePixels_ = mainCanvasSize;
        frameData_.voxelRenderOptions_.y = uncappedSub;
        frameDataBuf_->subData(0, sizeof(FrameDataVoxelToCanvas), &frameData_);
    }

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
            IRRender::getEffectiveCameraIso(),
            IRRender::getCameraZoom(),
            triangleCanvasTextures.size_
        );

        buildVoxelFrameData(
            frameData_,
            triangleCanvasTextures,
            liveVoxelCount,
            canvasLocalRotation
        );

        // Re-voxelize zoom-clip cap (#1570 D2). A re-voxelize detached canvas
        // rasters its pool in model space into a fixed-size canvas, but effSub
        // folds in camera zoom, so a zoom-scaled lattice overflows the canvas and
        // on-screen faces clip to background. Clamp the density to what the canvas
        // holds (the single-canvas analogue of the per-axis #1431 cap). Applied
        // here — before the UBO upload + compact dispatch below — so the compact
        // pass sizes the indirect Z count from the capped value too (no skip guard
        // needed). Gated on re-voxelize so the main world canvas and the
        // forward-scatter detached canvases stay byte-identical.
        if (canvasLocalRotation.isDetached() && canvasLocalRotation.reVoxelize_) {
            const int cap = IRPrefab::DetachedRevoxelize::subdivisionCap(
                triangleCanvasTextures.size_,
                voxelPool.getVoxelPoolSize3D()
            );
            frameData_.voxelRenderOptions_.y =
                IRMath::clamp(frameData_.voxelRenderOptions_.y, 1, cap);
        }

        // Detached re-voxelize fill mode (#1556 / #1619). The INVERSE path (#1619,
        // rotating) dispatches over the dest-cell cube and the GPU authors
        // position + color + active for those slots, so the shared compact + frame
        // `voxelCount` walk D dest slots (not the source count) and the CPU
        // color/active uploads below are skipped. At identity the source path runs
        // and everything stays byte-identical to #1556.
        C_DetachedRevoxelizeBuffer *revoxBuffer =
            canvasLocalRotation.reVoxelize_ ? lookupDetachedRevoxelizeBuffer(entity) : nullptr;
        const bool revoxInverse = revoxBuffer != nullptr && revoxBuffer->isAllocated() &&
                                  revoxBuffer->destCount_ > 0 &&
                                  canvasLocalRotation.rotation_ != vec4(0.0f, 0.0f, 0.0f, 1.0f);
        const int effectiveVoxelCount = revoxInverse ? revoxBuffer->destCount_ : liveVoxelCount;
        frameData_.voxelCount_ = effectiveVoxelCount;

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

        // Shadow-feeder params are resolved once per frame in beginTick
        // (frameShadowFeederParams), so no C_LightSource archetype scan runs
        // per entity; shadowFeederCullViewport reuses them at both margins.
        const IsoBounds2D chunkVp =
            IRPrefab::SunShadow::shadowFeederCullViewport(kCullChunkMargin, shadowFeederParams_);
        const CardinalIndex chunkCardinal = IRMath::rasterYawCardinalIndex(frameData_.rasterYaw_);
        // Smooth camera Z-yaw (T3 / #1310): while rotating (residual yaw != 0,
        // i.e. the per-axis canvases are active), project the chunk-visibility
        // gate with the same continuous yaw the per-axis scatter raster uses, so
        // off-center chunks aren't dropped by the cardinal snap. residual == 0
        // keeps the byte-identical cardinal path.
        const bool rotating = frameData_.residualYaw_ != 0.0f;
        if (revoxInverse) {
            // Dest-cell domain (#1619): the rotated solid rasters in its own
            // model-space canvas (camera pan/yaw zeroed), so the whole solid is
            // on-canvas — mark every dest-slot chunk visible. rebuildChunkBounds
            // keys chunks by SOURCE slot, which no longer matches the dest-slot
            // domain the compact walks.
            const int chunkWords = IRMath::divCeil(effectiveVoxelCount, IRRender::kVoxelChunkSize);
            if (static_cast<int>(allVisibleChunkScratch_.size()) < chunkWords) {
                allVisibleChunkScratch_.resize(chunkWords, 1u);
            }
            chunkVisBuf_->subData(
                0,
                static_cast<std::size_t>(chunkWords) * sizeof(std::uint32_t),
                allVisibleChunkScratch_.data()
            );
        } else {
            const auto &uploadMask = buildChunkVisibilityMask(
                voxelPool,
                chunkVp,
                chunkCardinal,
                rotating,
                frameData_.visualYaw_
            );
            chunkVisBuf_->subData(0, uploadMask.size() * sizeof(std::uint32_t), uploadMask.data());
        }

        constexpr int kGpuMargin = 4;
        const IsoBounds2D gpuVp =
            IRPrefab::SunShadow::shadowFeederCullViewport(kGpuMargin, shadowFeederParams_);
        frameData_.cullIsoMin_ = ivec2(IRMath::floor(gpuVp.min_));
        frameData_.cullIsoMax_ = ivec2(IRMath::ceil(gpuVp.max_));
        frameDataBuf_->subData(0, sizeof(FrameDataVoxelToCanvas), &frameData_);

        // Voxel positions are uploaded via the pending-range queue populated by
        // `UPDATE_VOXEL_SET_CHILDREN` (`cpp-ecs.md` pending-list-flush rule).
        // Steady-state: queue empty for static entities → zero subData bytes/frame;
        // moving entities coalesce into runs. On a canvas switch the queue is
        // drained and the static-transform runs are re-seeded from the CPU mirror
        // (`flushStaticPositionRanges`). GPU-transformed slots are left intact —
        // `UPDATE_VOXEL_POSITIONS_GPU` already wrote their correct world positions
        // into binding 5 ahead of this stage, and re-uploading the CPU mirror
        // would clobber the prepass output with translation-only data.
        {
            IR_PROFILE_SCOPE("vs1_pos");
            if (revoxBuffer != nullptr && revoxBuffer->isAllocated()) {
                // Detached re-voxelize (#1556 / #1619): the GPU compute owns binding
                // 5 (and, in the inverse path, color + active) for this pool — fill
                // it from this frame's quat in place of flushStaticPositionRanges.
                // Every frame, since the quat changes. Mark this canvas as the last
                // uploader so a later switch to a CPU-owned canvas re-seeds binding
                // 5 from its mirror, and drop any (empty) pending ranges.
                dispatchReVoxelize(*revoxBuffer, canvasLocalRotation, liveVoxelCount, revoxInverse);
                voxelPool.clearPendingPositionRanges();
                lastUploadedCanvas_ = entity;
            } else if (lastUploadedCanvas_ != entity) {
                // Re-seed only static-transform voxel runs so the GPU-prepass
                // output (UPDATE_VOXEL_POSITIONS_GPU, binding 5) is preserved
                // for GPU-transformed slots across canvas switches.
                flushStaticPositionRanges(voxelPool, voxelPosBuf_, liveVoxelCount);
                voxelPool.clearPendingPositionRanges();
                lastUploadedCanvas_ = entity;
            } else {
                flushPendingPositionRanges(voxelPool, voxelPosBuf_);
            }
        }
        // Color + active uploads are source-indexed (slot == source voxel). The
        // inverse-resample path (#1619) authors both on the GPU per DEST slot
        // instead, so skip the CPU uploads there — they would clobber the GPU's
        // dest-cell color and the atomic-OR'd active bits with source-indexed data.
        if (!revoxInverse) {
            voxelColorBuf_
                ->subData(0, liveVoxelCount * sizeof(C_Voxel), voxelPool.getColors().data());
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
            gpuStageTiming().totalVoxelCount_ = static_cast<std::uint32_t>(effectiveVoxelCount);
            voxelCullAccumulator().record(
                previous.visibleCount,
                static_cast<std::uint32_t>(effectiveVoxelCount)
            );
        }

        const VoxelIndirectDispatchParams zeroed{};
        indirectBuf_->subData(0, sizeof(VoxelIndirectDispatchParams), &zeroed);

        compactProgram_->use();
        constexpr int kCompactLocalSize = 64;
        // Inverse-resample walks the D dest slots; the source path walks the live
        // source count. frameData_.voxelCount_ (the compact's per-slot guard) was
        // set to the same effectiveVoxelCount above.
        const int compactGroups = IRMath::divCeil(effectiveVoxelCount, kCompactLocalSize);
        const ivec2 compactGrid = voxelDispatchGridForCount(compactGroups);
        IRRender::device()->dispatchCompute(compactGrid.x, compactGrid.y, 1);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
        IRRender::device()->memoryBarrier(BarrierType::COMMAND);

        // #1619 step-0 diagnostic (remove when #1619 closes): fill/compact health
        // for the inverse path — active-bit population vs dest domain vs compact
        // survivor count, logged for the first few inverse frames. On Linux/GL
        // this reads live=1728 activeBits=1299 visibleCount=1299 for the solo
        // L-prism (fill + compact healthy); running the same build on
        // macOS/Metal localizes the Metal dropout to fill, compact, or raster
        // in one run.
        if (revoxInverse && revoxDebugBudget_ > 0) {
            --revoxDebugBudget_;
            VoxelIndirectDispatchParams after{};
            indirectBuf_->getSubData(0, sizeof(VoxelIndirectDispatchParams), &after);
            const int maskWords =
                IRMath::divCeil(effectiveVoxelCount, static_cast<int>(kVoxelActiveMaskBits));
            std::vector<std::uint32_t> maskMirror(static_cast<std::size_t>(maskWords));
            voxelActiveMaskBuf_->getSubData(
                0,
                static_cast<std::size_t>(maskWords) * sizeof(std::uint32_t),
                maskMirror.data()
            );
            int activeBits = 0;
            for (std::uint32_t wordValue : maskMirror) {
                activeBits += std::popcount(wordValue);
            }
            IRE_LOG_INFO(
                "revox dbg entity={}: live={} destSide={} destCount={} activeBits={} "
                "visibleCount={} cull=({},{})..({},{})",
                entity,
                liveVoxelCount,
                revoxBuffer->destSide_,
                effectiveVoxelCount,
                activeBits,
                after.visibleCount,
                frameData_.cullIsoMin_.x,
                frameData_.cullIsoMin_.y,
                frameData_.cullIsoMax_.x,
                frameData_.cullIsoMax_.y
            );
        }

        // Smooth camera Z-yaw (T3 / #1310): while the MAIN canvas's per-axis
        // canvases are active, SKIP the single-canvas voxel rasterization. The
        // per-axis dispatch below writes the voxels (smooth), and the framebuffer
        // scatter composites them; rasterizing the snapped voxels into the single
        // canvas too would double-draw them (the single canvas is composited for
        // its SDF / overlay content, which sits at the SAME depth as the smooth
        // copies → snapped ghosts). Skipping leaves the single canvas holding only
        // SHAPES_TO_TRIXEL / text / overlay content, which the composite draws
        // alongside the smooth voxels. The compact pass above still runs (the
        // per-axis dispatch reuses its compacted list). Detached entities (incl.
        // re-voxelize SO(3)) always take the single-canvas emit + blit, so this is
        // false for them, byte-identical to master.
        const bool skipSingleCanvasVoxels = entity == perAxisCanvasEntity_ &&
                                            perAxisCanvases_ != nullptr &&
                                            perAxisCanvases_->isAllocated();
        if (!skipSingleCanvasVoxels) {
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

        // Smooth camera Z-yaw (T2 / #1309): once the single-canvas pass above
        // has run (and left the compacted-voxel list + indirect params intact),
        // route the visible faces into the three per-axis canvases. Only the
        // main world canvas owns them, and only while rotating — at a cardinal
        // they are released (#1308 fast path) so this is skipped and the frame
        // stays byte-identical to master.
        if (entity == perAxisCanvasEntity_ && perAxisCanvases_ != nullptr &&
            perAxisCanvases_->isAllocated()) {
            dispatchPerAxisCanvases(*perAxisCanvases_);
        }
    }

    void beginTick() {
        // Resolve sun direction once per frame so the per-entity tick
        // reads the cached value instead of scanning C_LightSource
        // once per voxel-pool-canvas pair.
        shadowFeederParams_ = IRPrefab::SunShadow::frameShadowFeederParams();

        IREntity::EntityId backgroundCanvas = IRRender::getCanvas("background");
        auto background =
            IREntity::getComponentOptional<C_TriangleCanvasBackground>(backgroundCanvas);
        auto backgroundTextures =
            IREntity::getComponentOptional<C_TriangleCanvasTextures>(backgroundCanvas);
        if (background.has_value() && backgroundTextures.has_value()) {
            (*background.value()).clearCanvasWithBackground(*backgroundTextures.value());
        }

        // Allocate / release the main canvas's per-axis trixel canvases for
        // smooth camera Z-yaw (#1308). Idempotent and once-per-frame; only
        // transitions on rotation start/stop. No faces route here in T1, so this
        // never alters the rendered output — it just stands up the storage that
        // T2 (#1309) routing will write into.
        IRPrefab::PerAxisCanvas::syncAllocationToCameraYaw();

        // Same lazy lifecycle for DETACHED_REVOXELIZE pools' resident GPU locals
        // (#1556): allocate + seed each pool's rigid locals once, and report the
        // live {canvas, &buffer} set the per-entity tick dispatches against (in
        // place of the CPU position flush). No-op for a scene with no re-voxelize
        // canvas — the list comes back empty.
        IRPrefab::DetachedRevoxelize::syncResidentBuffers(&detachedRevoxelizeBuffers_);

        // Resolve the main canvas's per-axis trixel canvases once per frame for
        // the per-entity tick to consume without a getComponent on its own
        // iterating canvas (#1309). Re-resolved every frame; never held across
        // frames. Null unless the main canvas has the component AND it is
        // currently allocated (camera rotating).
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
        // Detached re-voxelize GPU scatter compute + its per-frame params UBO
        // (#1556). The resident locals SSBO is owned per-canvas by
        // C_DetachedRevoxelizeBuffer (allocated lazily), so only the program and
        // the single-instance params buffer live here.
        IRRender::createNamedResource<ShaderProgram>(
            "RevoxelizeDetachedProgram",
            std::vector{ShaderStage{IRRender::kFileCompRevoxelizeDetached, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<Buffer>(
            "RevoxelizeDetachedParamsBuffer",
            nullptr,
            sizeof(RevoxelizeDetachedParams),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_RevoxelizeDetachedParams
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
            maxSingleVoxels * sizeof(IRRender::VoxelGpuPosition),
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
        p->revoxelizeProgram_ =
            IRRender::getNamedResource<ShaderProgram>("RevoxelizeDetachedProgram");
        p->revoxelizeParamsBuf_ =
            IRRender::getNamedResource<Buffer>("RevoxelizeDetachedParamsBuffer");
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
