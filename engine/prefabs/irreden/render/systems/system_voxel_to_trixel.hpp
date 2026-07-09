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
#include <irreden/render/shapes_2d.hpp>
#include <irreden/render/voxel_dispatch_grid.hpp>
#include <irreden/render/voxel_frame_data.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/components/component_detached_revoxelize_buffer.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>

#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>
#include <irreden/render/gpu_substage_timing.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
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

// Mirrors `kMaxHiZMipLevels` in c_chunk_occlusion_cull.{glsl,metal}; CPU
// binds [0, mipCount) to real levels and fills the surplus with the coarsest.
constexpr int kChunkOcclusionMaxHiZLevels = 12;

// Per-chunk Hi-Z occlusion query (#1294 child 2/3). Mirrors `ChunkQuery` in
// c_chunk_occlusion_cull.{glsl,metal} (std430, 32 B). Record 0 of the upload is
// a header: `pixelMin_` carries (chunkCount, mipCount).
struct ChunkOcclusionQuery {
    ivec2 pixelMin_{0, 0};
    ivec2 pixelMax_{0, 0};
    std::int32_t encodedNearest_ = 0;
    std::int32_t eligible_ = 0;
    std::int32_t pad0_ = 0;
    std::int32_t pad1_ = 0;
};
static_assert(
    sizeof(ChunkOcclusionQuery) == 32, "ChunkOcclusionQuery must match the std430 ChunkQuery stride"
);

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
    Buffer *compactedBuf_ = nullptr;
    // Chunk-occlusion HZB pre-pass (#1294 child 2/3, off by default). The query
    // buffer is bound transiently on kBufferIndex_CompactedVoxelIndices (25) for
    // the pre-pass and the compacted-index buffer restored afterward — the Metal
    // 0-30 buffer table has no free index. `chunkOcclusionScratch_` holds the
    // per-frame CPU query upload (header at [0], one record per chunk after).
    ShaderProgram *occlusionProgram_ = nullptr;
    Buffer *chunkOcclusionQueryBuf_ = nullptr;
    std::vector<ChunkOcclusionQuery> chunkOcclusionScratch_;
    int maxPoolChunks_ = 0;
    // One-frame occlusion-cull disable on a discontinuous camera move (#1294
    // child 3/3, design § 4). The chunk-occlusion pre-pass samples last frame's
    // Hi-Z; on a camera cut/teleport/first frame that lag source belongs to a
    // different view, so the projected chunk AABBs would test unrelated depths
    // and could cull on-screen geometry. When this frame's camera iso jumps more
    // than a fraction of the visible viewport, the cull is disabled for that one
    // frame — always the safe direction (keeps every chunk). Resolved once per
    // frame in beginTick; read by the per-canvas gate.
    vec2 lastOcclusionCameraIso_ = vec2(0.0f);
    bool hasLastOcclusionCameraIso_ = false;
    bool occlusionLagSourceStale_ = false;
    // Per-axis store list-walk split (#1739). While the main canvas's per-axis
    // trixel canvases are active (smooth camera Z-yaw), the compact pass splits
    // its visible-voxel list into three axis-keyed regions — each voxel landing
    // in the regions whose axis it has an exposed face on — so each per-axis
    // dispatch walks only its ~1/3 region instead of re-reading the full list
    // 6× (3 axes × stage1/stage2) and rejecting 2/3 of its threads. No new
    // persistent bind point (the Metal buffer-index budget is full at 30):
    // these are bound onto 25/26 transiently for the per-axis dispatch via
    // bindRange, then 25/26 are restored to the full compact buffers.
    // perAxisRegionStride_ is the per-region element capacity (maxSingleVoxels
    // rounded up so each region's byte offset meets the SSBO offset alignment).
    Buffer *perAxisCompactedBuf_ = nullptr;
    Buffer *perAxisIndirectBuf_ = nullptr;
    int perAxisRegionStride_ = 0;
    // Each per-axis list region and indirect-params struct starts at a multiple
    // of this byte boundary so the per-axis dispatch can bindRange them onto
    // 25/26 (GL requires SSBO bindRange offsets to be multiples of
    // GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, <= 256 on real hardware). The
    // compact shader mirrors the indirect stride as kPerAxisIndirectStrideUints.
    static constexpr int kPerAxisSsboAlignBytes = 256;
    // Per-axis empty-cell compaction (#1961 / #2256). Runs right after the
    // per-axis stores each frame; scans each axis distance canvas into the
    // component-owned compacted-cell buffers so the downstream per-axis compute
    // stages (AO / sun-shadow / lighting / resolve) and the framebuffer scatter
    // process only occupied cells. The buffers live on C_PerAxisTrixelCanvases
    // (same rotation-only lifecycle); this system only owns the shader program.
    ShaderProgram *cellCompactProgram_ = nullptr;
    // #2256: cheap 3-thread pass that derives the per-axis compute-indirect
    // dispatch dims from each axis's occupied count (kept off the compaction's
    // full-grid scan so that scan stays barrier-free).
    ShaderProgram *cellFinalizeProgram_ = nullptr;
    static constexpr int kPerAxisCellCompactGroupSize = 16;
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

    // Fog-of-war column cull (#2008): a shared 1×1 all-visible texture bound at
    // image slot 0 of the compact dispatch for every canvas that has no
    // C_CanvasFogOfWar (detached, GUI, non-fog creations). The compact shader
    // short-circuits on imageSize().x <= 1, so the placeholder is a true no-op —
    // it exists only to satisfy Metal's "every bound texture slot must be
    // populated" requirement. The real 256² fog texture is bound for the world
    // fog canvas instead.
    Texture2D *fogCullPlaceholder_ = nullptr;
    // Analytic vision-circle UBO for the compact's fog cull (slot 27). Uploaded
    // + bound just before the compact so a column a live vision circle covers
    // survives the grid-only cull — the voxel-floor smooth-fog case. Its own
    // buffer (not FOG_TO_TRIXEL's) so STAGE_1 carries no creation-order
    // dependency and reads the CURRENT frame's circles, not a frame-stale copy.
    Buffer *fogObserverBuf_ = nullptr;
    // Reusable .r → RGBA8 expansion scratch for the relocated fog upload
    // (#2008). System ticks are serial, so one shared buffer keeps the
    // per-dirty-frame upload allocation-free across however many fog canvases
    // exist; the value-init resize zeros the GBA bytes the loop never writes.
    std::vector<std::uint8_t> fogUploadScratch_;
    // The MAIN canvas's fog component, resolved + uploaded once per frame in
    // beginTick (#2127). A detached re-voxelize canvas carries no C_CanvasFogOfWar
    // of its own, but a WORLD-PLACED one cross-sections against the world vision
    // boundary, so its STAGE_1/STAGE_2 dispatch binds THIS world fog grid +
    // observers (recovering each voxel's world column from detachedWorldReceive).
    // Re-resolved every frame; never held across frames. Null unless the main
    // canvas has fog.
    C_CanvasFogOfWar *worldFog_ = nullptr;

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
    void dispatchPerAxisCanvases(C_PerAxisTrixelCanvases &axes, C_CanvasFogOfWar *fog) {
        IR_PROFILE_SCOPE("vs1_per_axis");
        // Fog cut-face / own-column-clip input for the per-axis rotation route
        // (#2128): the real 256² fog grid on the main world canvas, else the 1×1
        // all-visible placeholder so a rotating non-fog scene short-circuits the
        // shader test and stays byte-identical. The live vision circles were
        // uploaded into fogObserverBuf_ by the compact pass earlier in this same
        // per-canvas tick (the per-axis canvas is always the main canvas), so we
        // only bind it here — mirror of the single-canvas STAGE_1/2 binds.
        Texture2D *fogTex = (fog != nullptr) ? fog->getTexture() : fogCullPlaceholder_;
        // Per-axis canvas uses the fractional-offset encoding (#1458); valid
        // values exceed kTrixelDistanceMaxDistance, so INT_MAX is the sentinel.
        static constexpr std::int32_t kDistanceClear =
            static_cast<std::int32_t>(IRConstants::kPerAxisTrixelDistanceEmpty);
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
        // #2255: the winner scratch is one uint per per-axis texel; bound once
        // here for the per-axis winner-resolve + stage-2 guard (transient reuse
        // of kBufferIndex_PerAxisResolveScratch — free during the per-axis
        // store; the #1435 resolve + BAKE consumers re-bind it themselves).
        const std::size_t winnerScratchBytes = static_cast<std::size_t>(axes.size_.x) *
                                               static_cast<std::size_t>(axes.size_.y) *
                                               sizeof(std::uint32_t);
        axes.winnerIds_.second->bindBase(
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_PerAxisResolveScratch
        );
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
            // #2255: reset the winner scratch to the no-winner sentinel
            // (0xFFFFFFFF — a repeating byte, so both backends fill GPU-side)
            // before this axis's winner-resolve dispatch below.
            IRRender::device()->fillBuffer(axes.winnerIds_.second, winnerScratchBytes, 0xFFu);

            frameData_.perAxisRoute_ = axis + 1;
            frameData_.trixelCanvasOffsetZ1_ = perAxisOffsetZ1;
            frameData_.canvasSizePixels_ = axes.size_;
            frameDataBuf_->subData(0, sizeof(FrameDataVoxelToCanvas), &frameData_);

            // Per-axis store list-walk split (#1739): this axis dispatches over
            // only its own ~1/3 region of the compacted list (every voxel in it
            // already has an exposed face on this axis), so both stages walk a
            // third of the voxels instead of the full list, with the store
            // shader's (faceId>>1)!=axis reject pruning the other two of the
            // workgroup's three visible-triplet slots. Bind this axis's region +
            // indirect-params struct onto the 25/26 the store shaders read; the
            // store-shader SSBO declarations are unchanged (they read from offset
            // 0 of the bound range). The compact filled these in the split pass
            // above. Offsets are kPerAxisSsboAlignBytes-aligned by construction.
            const std::ptrdiff_t regionOffsetBytes = static_cast<std::ptrdiff_t>(axis) *
                                                     perAxisRegionStride_ *
                                                     static_cast<int>(sizeof(std::uint32_t));
            const std::ptrdiff_t indirectOffsetBytes =
                static_cast<std::ptrdiff_t>(axis) * kPerAxisSsboAlignBytes;
            perAxisCompactedBuf_->bindRange(
                BufferTarget::SHADER_STORAGE,
                kBufferIndex_CompactedVoxelIndices,
                regionOffsetBytes,
                static_cast<size_t>(perAxisRegionStride_) * sizeof(std::uint32_t)
            );
            perAxisIndirectBuf_->bindRange(
                BufferTarget::SHADER_STORAGE,
                kBufferIndex_IndirectDispatchParams,
                indirectOffsetBytes,
                sizeof(VoxelIndirectDispatchParams)
            );

            stage1Program_->use();
            // STAGE_1 reads the fog grid (slot 0) + observers (binding 27) for its
            // per-voxel fog clip (#2102) + cut-face test (#2125). The per-axis
            // rotation route now runs that clip/cut too (#2128), so bind the REAL
            // fog grid (or the placeholder on a non-fog canvas — short-circuits,
            // byte-identical) + the live observer buffer here, not a no-op
            // placeholder. Without the live grid a rotating boundary object would
            // render its hidden half as black hard-fog instead of clipping + cut.
            fogTex->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
            fogObserverBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FogObservers);
            distances->bindAsImage(1, TextureAccess::READ_ONLY, TextureFormat::R32I);
            IRRender::device()->dispatchComputeIndirect(perAxisIndirectBuf_, indirectOffsetBytes);
            IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);

            // #2255 winner-resolve dispatch: re-run stage 1 over this axis's
            // region with resolveMode=1 — among the faces whose encoded
            // distance ties the now-settled per-cell atomicMin winner, elect
            // the minimum run-stable voxel pool index into the winner
            // scratch. Stage 2's per-axis tap then admits exactly one tied
            // face, so the color/entity-id planes are byte-identical
            // run-to-run at a fixed pose (the distance plane always was).
            // stage1Program_ is still the active program and every bind
            // persists; only the mode field changes. One extra dispatch per
            // axis (plan-accepted).
            frameData_.resolveMode_ = 1;
            frameDataBuf_->subData(
                offsetof(FrameDataVoxelToCanvas, resolveMode_),
                sizeof(int),
                &frameData_.resolveMode_
            );
            IRRender::device()->dispatchComputeIndirect(perAxisIndirectBuf_, indirectOffsetBytes);
            // The winner SSBO writes must land before stage 2's guard reads.
            IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
            frameData_.resolveMode_ = 0;
            frameDataBuf_->subData(
                offsetof(FrameDataVoxelToCanvas, resolveMode_),
                sizeof(int),
                &frameData_.resolveMode_
            );

            stage2Program_->use();
            colors->bindAsImage(0, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
            distances->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::R32I);
            entityIds->bindAsImage(2, TextureAccess::WRITE_ONLY, TextureFormat::RG32UI);
            // STAGE_2 re-evaluates STAGE_1's cut-face predicate (#2125/#2128) so its
            // colour tap lands on the same faces. Slot 0 is the colour output here,
            // so the fog grid binds on slot 3 (slots 1/2 = distance + entity-id).
            // Same real-grid-or-placeholder choice as STAGE_1 above.
            fogTex->bindAsImage(3, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
            fogObserverBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FogObservers);
            IRRender::device()->dispatchComputeIndirect(perAxisIndirectBuf_, indirectOffsetBytes);
            IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
        }

        // Restore the full compact buffers to 25/26 so the next canvas's compact +
        // single-canvas path read/write them (the split pass + the bindRange loop
        // above left the per-axis buffers bound there).
        compactedBuf_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_CompactedVoxelIndices);
        indirectBuf_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_IndirectDispatchParams);

        // Restore the main-canvas frame data so downstream stages read the
        // exact UBO state the single-canvas pass left.
        frameData_.perAxisRoute_ = 0;
        frameData_.trixelCanvasOffsetZ1_ = mainOffsetZ1;
        frameData_.canvasSizePixels_ = mainCanvasSize;
        frameData_.voxelRenderOptions_.y = uncappedSub;
        frameDataBuf_->subData(0, sizeof(FrameDataVoxelToCanvas), &frameData_);
    }

    // Per-axis empty-cell compaction (#1961 / #2256). Scan each per-axis distance
    // canvas and append its occupied cells into the component-owned compacted-cell
    // region, filling the indirect args the framebuffer scatter DRAWS from AND the
    // compute-indirect dispatch dims the per-axis AO / sun-shadow / lighting /
    // resolve stages DISPATCH from. Runs right after the per-axis stores (the axis
    // distance canvases are fully written + image-barrier'd inside
    // dispatchPerAxisCanvases). Borrows slots 25/26 transiently, then restores them
    // to the voxel single-canvas compaction buffers (STAGE_1 rebinds those sticky).
    void compactPerAxisCells(C_PerAxisTrixelCanvases &axes) {
        Buffer *cellCompacted = axes.cellCompacted_.second;
        Buffer *cellIndirect = axes.cellIndirect_.second;
        if (cellCompacted == nullptr || cellIndirect == nullptr) {
            return;
        }
        const ivec2 axisSize = axes.size_;
        const int regionStride = axes.cellRegionStride_;

        // Reset each axis's 256 B indirect region to zero (clears instanceCount and
        // the compute-indirect dispatch dims), then set the fixed draw-command
        // indexCount. The compaction atomic-appends instanceCount; the finalize
        // pass writes the dispatch dims from the final count.
        std::array<std::uint32_t, kPerAxisCellIndirectStrideBytes / sizeof(std::uint32_t)>
            resetRegion{};
        resetRegion[0] = static_cast<std::uint32_t>(IRShapes2D::kQuadIndicesLength);
        for (int axis = 0; axis < C_PerAxisTrixelCanvases::kAxisCount; ++axis) {
            cellIndirect->subData(
                static_cast<std::ptrdiff_t>(axis) * kPerAxisCellIndirectStrideBytes,
                sizeof(resetRegion),
                resetRegion.data()
            );
        }

        cellCompactProgram_->use();
        const int groupsX = IRMath::divCeil(axisSize.x, kPerAxisCellCompactGroupSize);
        const int groupsY = IRMath::divCeil(axisSize.y, kPerAxisCellCompactGroupSize);
        for (int axis = 0; axis < C_PerAxisTrixelCanvases::kAxisCount; ++axis) {
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
            axes.axes_[axis]
                .distances_.second->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
            IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
        }
        // Make each axis's occupied count (instanceCount) visible to the finalize.
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);

        // #2256: derive the per-axis compute-indirect dispatch dims from the final
        // occupied counts (a cheap 3-thread pass — one axis per workgroup over the
        // whole indirect buffer). Split out of the compaction so its full-grid scan
        // stays barrier-free.
        cellFinalizeProgram_->use();
        cellIndirect->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_PerAxisCellIndirect);
        IRRender::device()->dispatchCompute(C_PerAxisTrixelCanvases::kAxisCount, 1, 1);

        // The compacted list + the per-axis compute-indirect params feed subsequent
        // COMPUTE dispatches (SHADER_STORAGE) and the scatter's indirect DRAW reads
        // its draw args as a command source (COMMAND) — barrier both before any
        // consumer runs.
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
        IRRender::device()->memoryBarrier(BarrierType::COMMAND);

        // Restore 25/26 to the voxel single-canvas compaction buffers (STAGE_1's
        // next-frame compact relies on those sticky binds). Downstream per-axis
        // consumers rebind the cell buffers transiently themselves.
        compactedBuf_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_CompactedVoxelIndices);
        indirectBuf_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_IndirectDispatchParams);
    }

    // Chunk-occlusion HZB pre-pass (#1294 child 2/3). Runs after the frustum
    // ChunkVisibility mask + frame-data are uploaded and BEFORE the compact pass,
    // so the compact reads the AND of frustum ∧ occlusion. Tests last frame's
    // Hi-Z (#1798); only fully-on-screen NONE-mode cardinal chunks are eligible
    // (shadow-feeder-safe by construction — off-screen casters fall outside the
    // visible viewport and are never tested). Conservative: a footprint still
    // seeing background (65535) keeps the chunk. Caller gates the whole pass off
    // by default, so a default scene never reaches here.
    void dispatchChunkOcclusion(
        C_VoxelPool &voxelPool, const C_TriangleCanvasTextures &canvas, const IsoBounds2D &visibleVp
    ) {
        const int mipCount = canvas.hiZMipCount();
        if (mipCount <= 0)
            return;
        const auto &bounds = voxelPool.getChunkBounds();
        const int chunkCount = static_cast<int>(bounds.size());
        if (chunkCount == 0)
            return;
        IR_ASSERT(
            chunkCount <= maxPoolChunks_,
            "dispatchChunkOcclusion: chunkCount {} exceeds query-buffer capacity {}",
            chunkCount,
            maxPoolChunks_
        );

        // iso -> canvas pixel exactly as stage 1 at NONE mode
        // (effectiveTrixelSubdivisionScale == 1): canvasPixel =
        // trixelCanvasOffsetZ1 + floor(cameraIso) + isoPos.
        const ivec2 frameOffset =
            frameData_.trixelCanvasOffsetZ1_ + ivec2(IRMath::floor(frameData_.cameraTrixelOffset_));

        chunkOcclusionScratch_.assign(chunkCount + 1, ChunkOcclusionQuery{});
        chunkOcclusionScratch_[0].pixelMin_ = ivec2(chunkCount, mipCount);

        constexpr float kNoDepth = std::numeric_limits<float>::max();
        for (int c = 0; c < chunkCount; ++c) {
            const ChunkBounds &cb = bounds[c];
            ChunkOcclusionQuery &q = chunkOcclusionScratch_[c + 1];
            // Eligible only when the chunk has live voxels with a tracked depth
            // AND its iso AABB sits fully inside the VISIBLE viewport — never an
            // off-screen shadow feeder (which lives in the wider swept viewport).
            const bool hasVoxels = cb.isoMin_.x <= cb.isoMax_.x && cb.minDepth_ < kNoDepth;
            const bool fullyVisible =
                hasVoxels && cb.isoMin_.x >= visibleVp.min_.x && cb.isoMin_.y >= visibleVp.min_.y &&
                cb.isoMax_.x <= visibleVp.max_.x && cb.isoMax_.y <= visibleVp.max_.y;
            if (fullyVisible) {
                q.pixelMin_ = frameOffset + ivec2(IRMath::floor(cb.isoMin_));
                q.pixelMax_ = frameOffset + ivec2(IRMath::ceil(cb.isoMax_));
                // Encode the chunk's nearest depth like trixelDistances at NONE
                // mode (encodeDepthWithFace: rawDepth * kDepthEncodeShift,
                // flip 0, face slot 0).
                q.encodedNearest_ = static_cast<std::int32_t>(cb.minDepth_) * kDepthEncodeShift;
                q.eligible_ = 1;
            }
        }

        chunkOcclusionQueryBuf_->subData(
            0,
            chunkOcclusionScratch_.size() * sizeof(ChunkOcclusionQuery),
            chunkOcclusionScratch_.data()
        );

        occlusionProgram_->use();
        chunkOcclusionQueryBuf_->bindBase(
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_CompactedVoxelIndices
        );
        // chunkVisBuf_ stays bound at kBufferIndex_ChunkVisibility (24). Bind the
        // Hi-Z downsampled levels [0, mipCount) as sampled images; surplus sampler
        // slots get the coarsest level (never sampled — mip selection clamps).
        for (int u = 0; u < kChunkOcclusionMaxHiZLevels; ++u) {
            canvas.getHiZMip(IRMath::min(u, mipCount - 1))->bind(u);
        }

        // Matches local_size_x in c_chunk_occlusion_cull.{glsl,metal}.
        constexpr int kOcclusionLocalSize = 64;
        const ivec2 grid =
            voxelDispatchGridForCount(IRMath::divCeil(chunkCount, kOcclusionLocalSize));
        IRRender::device()->dispatchCompute(grid.x, grid.y, 1);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);

        // Restore the compacted-index buffer on slot 25 for the compact pass.
        compactedBuf_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_CompactedVoxelIndices);
    }

    // Relocated from FOG_TO_TRIXEL (#2008): push the CPU fog mirror to its GPU
    // texture once per dirty frame. STAGE_1 now owns the upload so the column
    // cull below — and the later FOG_TO_TRIXEL post-process — read the SAME,
    // current-frame fog (no one-frame lag, no startup-frame garbage). The
    // dirty-flag exception (CPU-authored, GPU-read-only, 256 KiB whole-texture
    // upload) is unchanged; only the system performing the upload moved.
    void uploadFogIfDirty(C_CanvasFogOfWar &fog) {
        if (!fog.dirty_) {
            return;
        }
        const std::size_t cellCount = fog.cpuBuffer_.size();
        if (fogUploadScratch_.size() < cellCount * 4) {
            fogUploadScratch_.resize(cellCount * 4);
        }
        // Only the .r channel ever changes; GBA stay at the resize zero-init.
        for (std::size_t i = 0; i < cellCount; ++i) {
            fogUploadScratch_[i * 4] = fog.cpuBuffer_[i];
        }
        fog.getTexture()->subImage2D(
            0,
            0,
            kFogOfWarSize,
            kFogOfWarSize,
            PixelDataFormat::RGBA,
            PixelDataType::UNSIGNED_BYTE,
            fogUploadScratch_.data()
        );
        fog.dirty_ = false;
    }

    void tick(
        IREntity::EntityId entity,
        C_VoxelPool &voxelPool,
        C_TriangleCanvasTextures &triangleCanvasTextures,
        const C_CanvasLocalRotation &canvasLocalRotation
    ) {
        // CPU whole-tick timing (#2280): this system is no longer tagged for the
        // per-system GpuStageTimingObserver (which used to supply both the CPU
        // and GPU `voxelStage1` samples), so record the CPU side here to keep
        // the HUD's / auto-profile's `voxelStage1` CPU number. Note the
        // intentional asymmetry the sub-attribution introduces: CPU `voxelStage1`
        // stays the WHOLE per-canvas tick, while GPU `voxelStage1` now measures
        // only the stage-1 dispatch (the compact / clear / stage-2 GPU costs
        // moved to their own sub-rows).
        IR_PROFILE_SCOPE("voxelStage1");
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
            IRRender::GpuSubStageScope gpuScope("canvasClear");
            clearCanvasAndDistances(entity, triangleCanvasTextures);
        }

        // Fog-of-war column cull (#2008): resolve this canvas's optional fog
        // texture and flush any pending CPU edit to the GPU BEFORE the
        // early-return below, so a pure-SDF fog scene (no live voxels) still
        // uploads current fog for FOG_TO_TRIXEL's explored/unexplored masking.
        // `fog` is held for the compact-dispatch bind further down. The
        // getComponentOptional is on the iterating canvas — the accepted
        // per-canvas O(handful) pattern; same shape as LIGHTING_TO_TRIXEL's
        // C_CanvasSunShadow + C_CanvasLightVolume lookups (line 143-144), not
        // the per-voxel getComponent footgun.
        C_CanvasFogOfWar *fog = nullptr;
        {
            auto fogOpt = IREntity::getComponentOptional<C_CanvasFogOfWar>(entity);
            if (fogOpt.has_value()) {
                fog = fogOpt.value();
                uploadFogIfDirty(*fog);
            }
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
        //
        // The pool size feeding subdivisionCap was uninitialized before #2043 —
        // see C_VoxelPool::m_voxelPoolSize3D — which non-deterministically pinned
        // this cap (the #2043 root cause); it is now correct, so a generously-sized
        // canvas (footprint cap ≫ effSub) admits cubeSub > 1, which surfaces the
        // #2043 detached-canvas oversize. The cubeSub→apparent-size decoupling that
        // fixes that is a composite-side change (ENTITY_CANVAS_TO_FRAMEBUFFER divides
        // cubeSub out of the quad scale + gather density — #2043 Option A, see
        // docs/design/detached-canvas-density-compensation.md); it is NOT a
        // raster-side zoom-track here (camera zoom is clamped to ≥ 1 by
        // kTrixelCanvasZoomMin, so a zoom-track at this site can never lower the
        // density).
        if (canvasLocalRotation.isDetached() && canvasLocalRotation.reVoxelize_) {
            const int cap = IRPrefab::DetachedRevoxelize::subdivisionCap(
                triangleCanvasTextures.size_,
                voxelPool.getVoxelPoolSize3D()
            );
            frameData_.voxelRenderOptions_.y =
                IRMath::clamp(frameData_.voxelRenderOptions_.y, 1, cap);
        }

        // Publish the sub this canvas actually rastered at so the detached
        // composite can rescale this canvas's model-frame depth into the shared
        // framebuffer depth units (#1624 world-placed depth fix). For the main
        // world canvas this is the un-capped global effSub; for a capped
        // re-voxelize canvas it is the reduced value. Stamped for every voxel
        // canvas (the main canvas's value is simply never read by the detached
        // composite).
        triangleCanvasTextures.renderedSubdivisions_ = frameData_.voxelRenderOptions_.y;

        // No-priority perf fast-path (#2155). Publish whether any voxel in this
        // canvas's pool carries a non-zero per-trixel priority (#1960), maintained
        // push-at-mutation on the pool (O(1) read, no per-voxel scan). The
        // finalization gather (TRIXEL_TO_FRAMEBUFFER) and the detached composite
        // (ENTITY_CANVAS_TO_FRAMEBUFFER) forward it into the shader's UBO to gate
        // the per-fragment entity-id decode read. Covers the re-voxelize path too:
        // the private pool inherits its source set's priority via
        // changeVoxelPriority*, so a rotating priority solid still stamps 1.
        triangleCanvasTextures.anyPerTrixelPriority_ = voxelPool.hasPerTrixelPriority() ? 1 : 0;

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
        // keeps the byte-identical cardinal path. residualYaw_ is deadbanded at
        // its source (Camera::computeYawSplit / kResidualYawDeadband, #1882), so
        // this `!= 0` path-select predicate matches the per-axis allocation gate
        // exactly — a near-cardinal residual can't free the textures while still
        // routing here (the old gap that read freed textures as coverage holes).
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
        // Depth-only shadow-feeder path (#1740): the SAME viewport at the same
        // margin BEFORE shadowFeederCullViewport widened it toward the sun. A
        // voxel inside gpuVp (it passed the compact cull) but outside this is an
        // off-screen shadow feeder — stage 2 skips its colour/entity-id taps.
        // When sun shadows are off the sweep is zero, so gpuVp == this and stage
        // 2 skips nothing (byte-identical). Same getCullViewport() the widened
        // form reads above, so the two boxes are derived consistently.
        const IsoBounds2D visibleVp = IRRender::getCullViewport().isoViewport(kGpuMargin);
        frameData_.visibleIsoBounds_ =
            ivec4(ivec2(IRMath::floor(visibleVp.min_)), ivec2(IRMath::ceil(visibleVp.max_)));

        // Occlusion cull gate (#1294 chunk pre-pass + #1812 per-voxel refine, off
        // by default). Enabled only on the states whose distance encoding +
        // shadow-feeder semantics are verified: enabled, NOT stale (no
        // discontinuous camera move this frame — #1294 child 3/3, resolved in
        // beginTick), NONE render mode (encodeDepthWithFace = rawDepth*kDepthEncodeShift), cardinal
        // yaw (!rotating → per-axis canvases inactive, so no split-list
        // interaction), a non-re-voxelize pool (the frustum mask is the real
        // per-chunk mask, not the all-visible dest-cell scratch), AND a built Hi-Z
        // chain. Any other state keeps every voxel (conservative). The chunk
        // pre-pass ANDs occluded chunks out of ChunkVisibility (24); the compact
        // then runs the per-voxel Hi-Z test on the survivors (occlusionCullMipCount_
        // uploaded here, Hi-Z bound at the compact dispatch below).
        const int occlusionMipCount = triangleCanvasTextures.hiZMipCount();
        const bool occlusionCullActive =
            IRRender::getVoxelOcclusionCullEnabled() && !occlusionLagSourceStale_ &&
            frameData_.voxelRenderOptions_.x == 0 && !rotating && revoxBuffer == nullptr &&
            occlusionMipCount > 0;
        // The chunk pre-pass (dispatched below on occlusionCullActive) and the
        // per-voxel Hi-Z refine are separately toggleable so the #1812 marginal
        // acceptance gate can A/B the per-voxel test in isolation while the chunk
        // cull stays on: --no-per-voxel-occlusion zeroes the mip count (skips the
        // compact's per-voxel test) without touching dispatchChunkOcclusion. The
        // per-voxel toggle defaults on, so --occlusion-cull alone runs both.
        frameData_.occlusionCullMipCount_ =
            (occlusionCullActive && IRRender::getVoxelPerVoxelOcclusionEnabled())
                ? occlusionMipCount
                : 0;
        frameDataBuf_->subData(0, sizeof(FrameDataVoxelToCanvas), &frameData_);

        if (occlusionCullActive) {
            dispatchChunkOcclusion(voxelPool, triangleCanvasTextures, visibleVp);
        }

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

        // Smooth camera Z-yaw (T3 / #1310): while the MAIN canvas's per-axis
        // canvases are active, SKIP the single-canvas voxel rasterization. The
        // per-axis dispatch below writes the voxels (smooth), and the framebuffer
        // scatter composites them; rasterizing the snapped voxels into the single
        // canvas too would double-draw them (the single canvas is composited for
        // its SDF / overlay content, which sits at the SAME depth as the smooth
        // copies → snapped ghosts). Skipping leaves the single canvas holding only
        // SHAPES_TO_TRIXEL / text / overlay content, which the composite draws
        // alongside the smooth voxels. The compact pass below still runs (the
        // per-axis dispatch reuses its compacted list). Detached entities (incl.
        // re-voxelize SO(3)) always take the single-canvas emit + blit, so this is
        // false for them, byte-identical to master. Resolved before the cull
        // diagnostic so it can pick the matching indirect-dispatch-params source.
        const bool skipSingleCanvasVoxels = entity == perAxisCanvasEntity_ &&
                                            perAxisCanvases_ != nullptr &&
                                            perAxisCanvases_->isAllocated();

        // Cull diagnostic readback (gated by gpu_stage_timing.enabled_).
        // The indirect buffer still holds the prior frame's visibleCount;
        // reading it here — before we zero it for this frame's compact pass —
        // requires no explicit fence: the driver serializes the CPU read
        // against the prior frame's already-retired compact write.
        // Frame N+1 reads frame N's value; the first frame reports 0,
        // contributing a 1/N bias to the running average — negligible
        // over typical measurement runs.
        //
        // The source depends on the path the prior frame's compact took. The
        // single-canvas (cardinal) compact appends survivors into indirectBuf_,
        // but the per-axis split (#1739, active iff skipSingleCanvasVoxels)
        // routes its count into perAxisIndirectBuf_'s three axis regions and
        // leaves indirectBuf_ zeroed. Reading indirectBuf_ unconditionally
        // reported a spurious 0/total for every rotating (per-axis) frame
        // (#1856) — sum the three axis regions when the split path is active.
        // A voxel exposed on N axes is appended to N regions, so the per-axis
        // sum counts face-routings (≥ the unique visible-voxel count): the
        // "how much work the per-axis path does" cull-effectiveness signal the
        // overlay wants, not a strict voxel count comparable 1:1 to cardinal.
        if (gpuStageTiming().enabled_) {
            std::uint32_t visible = 0;
            if (skipSingleCanvasVoxels && perAxisIndirectBuf_ != nullptr) {
                for (int axis = 0; axis < C_PerAxisTrixelCanvases::kAxisCount; ++axis) {
                    VoxelIndirectDispatchParams region{};
                    perAxisIndirectBuf_->getSubData(
                        static_cast<std::ptrdiff_t>(axis) * kPerAxisSsboAlignBytes,
                        sizeof(VoxelIndirectDispatchParams),
                        &region
                    );
                    visible += region.visibleCount;
                }
            } else {
                VoxelIndirectDispatchParams previous{};
                indirectBuf_->getSubData(0, sizeof(VoxelIndirectDispatchParams), &previous);
                visible = previous.visibleCount;
            }
            gpuStageTiming().visibleVoxelCount_ = visible;
            gpuStageTiming().totalVoxelCount_ = static_cast<std::uint32_t>(effectiveVoxelCount);
            voxelCullAccumulator().record(visible, static_cast<std::uint32_t>(effectiveVoxelCount));
        }

        const VoxelIndirectDispatchParams zeroed{};
        indirectBuf_->subData(0, sizeof(VoxelIndirectDispatchParams), &zeroed);

        // Per-axis store list-walk split (#1739). For exactly the main-canvas-
        // rotating compact (whose voxels the per-axis dispatch consumes, and
        // whose single-canvas pass is skipped above) route the compact's writes
        // into the three axis-keyed regions instead of the single full list:
        // bind the per-axis buffers onto 25/26 and pass the region stride through
        // the otherwise-dead perAxisRoute_ slot (non-zero = split mode; the store
        // shaders never see this value — single-canvas is skipped and the per-axis
        // pass re-uploads perAxisRoute_ to 1/2/3). Every other compact keeps the
        // single full list, byte-identical to master.
        const bool perAxisSplit = skipSingleCanvasVoxels;
        if (perAxisSplit) {
            constexpr int kPerAxisIndirectWords =
                3 * kPerAxisSsboAlignBytes / static_cast<int>(sizeof(std::uint32_t));
            const std::uint32_t zeroedPerAxisIndirect[kPerAxisIndirectWords] = {};
            perAxisIndirectBuf_->subData(0, sizeof(zeroedPerAxisIndirect), zeroedPerAxisIndirect);
            perAxisCompactedBuf_->bindBase(
                BufferTarget::SHADER_STORAGE,
                kBufferIndex_CompactedVoxelIndices
            );
            perAxisIndirectBuf_->bindBase(
                BufferTarget::SHADER_STORAGE,
                kBufferIndex_IndirectDispatchParams
            );
            frameData_.perAxisRoute_ = perAxisRegionStride_;
            frameDataBuf_->subData(0, sizeof(FrameDataVoxelToCanvas), &frameData_);
        }

        // World fog source for the cut-section dispatch (#2125 / #2127). A canvas
        // with its OWN fog (the main world canvas) cross-sections against it
        // directly; a WORLD-PLACED detached re-voxelize canvas has no fog of its
        // own, so it cross-sections against the MAIN canvas's world fog (resolved +
        // uploaded in beginTick). STAGE_1/STAGE_2 recover each detached voxel's
        // world column from detachedWorldReceive; the compact below keeps the
        // placeholder because its cull keys on the un-offset model column. Plain
        // octahedral DETACHED / per-axis / non-fog → null → the no-fog placeholder.
        const bool worldPlacedRevoxel = canvasLocalRotation.isDetached() &&
                                        canvasLocalRotation.reVoxelize_ &&
                                        canvasLocalRotation.worldPlaced_;
        C_CanvasFogOfWar *cutSectionFog =
            (fog != nullptr) ? fog : (worldPlacedRevoxel ? worldFog_ : nullptr);

        compactProgram_->use();
        // Fog cull input (#2008): the real 256² fog texture for the world fog
        // canvas, else the shared 1×1 all-visible placeholder (compact shader
        // short-circuits on imageSize<=1 → no cull, byte-identical to master).
        // Bound right before the dispatch so an intervening chunk-occlusion
        // pre-pass can't leave a stale texture on image slot 0. The detached
        // cut-section path deliberately keeps the placeholder here (cutSectionFog
        // is only consumed by STAGE_1/STAGE_2, which apply the world-column offset
        // the compact's model-column cull cannot).
        (fog != nullptr ? fog->getTexture() : fogCullPlaceholder_)
            ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
        // Analytic vision-circle cull input: upload the CURRENT frame's circles
        // (FOG_TO_TRIXEL runs later, so its copy would be a frame stale) and bind
        // at slot 27. A column a live circle covers is kept even when its grid
        // cell is unexplored, so a voxel-floor scene driven purely by
        // setVisionCircle keeps its floor. The world-placed detached canvas uploads
        // the MAIN canvas's circles here (via cutSectionFog) so STAGE_1/STAGE_2 see
        // a non-zero count; non-fog canvases keep the seeded count-0 buffer (the
        // shader short-circuits on the placeholder anyway).
        if (cutSectionFog != nullptr) {
            fogObserverBuf_->subData(0, sizeof(FrameDataFogObservers), &cutSectionFog->observers_);
        }
        fogObserverBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FogObservers);
        // Per-voxel occlusion cull Hi-Z input (#1812). Bind the finest Hi-Z level
        // at a texture unit distinct from the fog IMAGE at 0 (GL image/texture
        // unit 0 alias; Metal's shared argument table needs the distinct index).
        // The compact samples it only when frameData_.occlusionCullMipCount_ > 0;
        // a canvas with no Hi-Z chain (≤1px) binds the R32I distance texture as a
        // never-sampled sentinel so Metal's argument table stays satisfied. Bound
        // every frame (one texture bind) — the shader gate keeps the default
        // (cull-off) output byte-identical.
        constexpr int kHiZLevel0CompactTextureUnit = 1;
        const Texture2D *hiZLevel0 = (triangleCanvasTextures.hiZMipCount() > 0)
                                         ? triangleCanvasTextures.getHiZMip(0)
                                         : triangleCanvasTextures.getTextureDistances();
        hiZLevel0->bind(kHiZLevel0CompactTextureUnit);
        constexpr int kCompactLocalSize = 64;
        // Inverse-resample walks the D dest slots; the source path walks the live
        // source count. frameData_.voxelCount_ (the compact's per-slot guard) was
        // set to the same effectiveVoxelCount above.
        const int compactGroups = IRMath::divCeil(effectiveVoxelCount, kCompactLocalSize);
        const ivec2 compactGrid = voxelDispatchGridForCount(compactGroups);
        {
            IRRender::GpuSubStageScope gpuScope("voxelCompact");
            IRRender::device()->dispatchCompute(compactGrid.x, compactGrid.y, 1);
            IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
            IRRender::device()->memoryBarrier(BarrierType::COMMAND);
        }

        if (perAxisSplit) {
            // Reset perAxisRoute_ for any downstream UBO read; the per-axis pass
            // sets it to 1/2/3 itself. 25/26 stay bound to the per-axis buffers —
            // dispatchPerAxisCanvases consumes them via bindRange and restores
            // 25/26 to the full compact buffers when it finishes.
            frameData_.perAxisRoute_ = 0;
            frameDataBuf_->subData(0, sizeof(FrameDataVoxelToCanvas), &frameData_);
        }

        if (!skipSingleCanvasVoxels) {
            stage1Program_->use();
            // Per-voxel analytic fog clip (#2102) + cut faces (#2125/#2127):
            // re-bind the fog grid (slot 0) + live vision circles (binding 27) for
            // STAGE_1. The compact bound these above and GL state persists across
            // the program switch, but Metal's per-encoder argument table needs them
            // set on this dispatch too. Real fog on the world fog canvas OR the main
            // canvas's world fog for a world-placed detached re-voxelize canvas
            // (cutSectionFog), else the 1×1 all-visible placeholder (the clip
            // no-ops, byte-identical).
            (cutSectionFog != nullptr ? cutSectionFog->getTexture() : fogCullPlaceholder_)
                ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
            fogObserverBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FogObservers);
            triangleCanvasTextures.getTextureDistances()
                ->bindAsImage(1, TextureAccess::READ_ONLY, TextureFormat::R32I);
            {
                IRRender::GpuSubStageScope gpuScope("voxelStage1");
                IRRender::device()->dispatchComputeIndirect(indirectBuf_, 0);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
            }

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
            // Fog cut-face inputs (#2125/#2127): STAGE_2 re-evaluates STAGE_1's
            // cut-face predicate so its colour tap lands on the same faces, so it
            // needs the fog grid + observers too. Slot 0 holds the colour output
            // here, so the fog grid binds on slot 3 (slots 1/2 = distance +
            // entity-id outputs). Real fog on the world fog canvas OR the main
            // canvas's world fog for a world-placed detached re-voxelize canvas
            // (cutSectionFog), else the 1×1 placeholder (the cut-face test no-ops,
            // byte-identical). Metal needs both bound on this dispatch since the
            // kernel declares them.
            (cutSectionFog != nullptr ? cutSectionFog->getTexture() : fogCullPlaceholder_)
                ->bindAsImage(3, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
            fogObserverBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FogObservers);
            {
                IRRender::GpuSubStageScope gpuScope("voxelStage2");
                IRRender::device()->dispatchComputeIndirect(indirectBuf_, 0);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
            }
        }

        // Smooth camera Z-yaw (T2 / #1309): once the single-canvas pass above
        // has run (and left the compacted-voxel list + indirect params intact),
        // route the visible faces into the three per-axis canvases. Only the
        // main world canvas owns them, and only while rotating — at a cardinal
        // they are released (#1308 fast path) so this is skipped and the frame
        // stays byte-identical to master.
        if (entity == perAxisCanvasEntity_ && perAxisCanvases_ != nullptr &&
            perAxisCanvases_->isAllocated()) {
            dispatchPerAxisCanvases(*perAxisCanvases_, fog);
            // #2256: compact each per-axis canvas's occupied cells NOW (the axis
            // distance canvases were just fully written above), so the downstream
            // per-axis compute stages (AO / sun-shadow / lighting / resolve) and
            // the framebuffer scatter can dispatch/draw over only occupied cells.
            // The distance-canvas writes are barrier'd inside dispatchPerAxisCanvases.
            compactPerAxisCells(*perAxisCanvases_);
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

        // Resolve the MAIN canvas's fog once per frame for a world-placed detached
        // re-voxelize canvas to cross-section against (#2127). Upload it current
        // HERE so the world fog grid is fresh regardless of canvas tick order — a
        // detached canvas may tick before the main canvas, whose own
        // uploadFogIfDirty then no-ops (dirty already cleared). Null on a scene with
        // no fog → the detached path falls back to the no-fog placeholder.
        worldFog_ = nullptr;
        if (perAxisCanvasEntity_ != IREntity::kNullEntity) {
            auto worldFogOpt =
                IREntity::getComponentOptional<C_CanvasFogOfWar>(perAxisCanvasEntity_);
            if (worldFogOpt.has_value()) {
                worldFog_ = worldFogOpt.value();
                uploadFogIfDirty(*worldFog_);
            }
        }

        // Resolve the one-frame occlusion-cull disable (#1294 child 3/3, design
        // § 4). The chunk-occlusion pre-pass tests last frame's Hi-Z, so on a
        // discontinuous camera move (cut / teleport / first frame) that lag
        // source is from an unrelated view and the cull is disabled for one
        // frame. The threshold is half the visible viewport's smaller iso extent
        // — larger than any single-frame smooth pan but far below a cut. Disabling
        // is always safe (keeps every chunk); a smooth pan's one-frame-lag pop is
        // the accepted intentional drift, not a discontinuity. Skipped entirely
        // when the cull is off so the default config adds nothing — the gate
        // short-circuits on getVoxelOcclusionCullEnabled() before reading the
        // staleness flag, and the first enabled frame is treated as a cut.
        if (IRRender::getVoxelOcclusionCullEnabled()) {
            const vec2 cameraIso = IRRender::getEffectiveCameraIso();
            const IsoBounds2D occlusionViewport = IRRender::getCullViewport().isoViewport();
            const vec2 viewportExtent = occlusionViewport.max_ - occlusionViewport.min_;
            const float cutThreshold = 0.5f * IRMath::min(viewportExtent.x, viewportExtent.y);
            occlusionLagSourceStale_ =
                !hasLastOcclusionCameraIso_ ||
                IRMath::length(cameraIso - lastOcclusionCameraIso_) > cutThreshold;
            lastOcclusionCameraIso_ = cameraIso;
            hasLastOcclusionCameraIso_ = true;
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
        // Chunk-occlusion HZB pre-pass program (#1294 child 2/3).
        IRRender::createNamedResource<ShaderProgram>(
            "ChunkOcclusionProgram",
            std::vector{ShaderStage{IRRender::kFileCompChunkOcclusionCull, ShaderType::COMPUTE}}
        );
        // Per-axis empty-cell compaction pre-pass (#1961 / #2256) — run in this
        // system right after the per-axis stores, feeding the per-axis compute
        // stages + the framebuffer scatter (see compactPerAxisCells).
        IRRender::createNamedResource<ShaderProgram>(
            "PerAxisCellCompactProgram",
            std::vector{ShaderStage{IRRender::kFileCompPerAxisCellCompact, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<ShaderProgram>(
            "PerAxisCellFinalizeProgram",
            std::vector{ShaderStage{IRRender::kFileCompPerAxisCellFinalize, ShaderType::COMPUTE}}
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
        // #2255: the stage-1/2 kernels declare the per-axis winner scratch at
        // kBufferIndex_PerAxisResolveScratch (28). The real scratch is
        // allocated lazily with the per-axis canvases; this placeholder
        // guarantees index 28 is never unbound for the single-canvas /
        // detached dispatches, which gate every access behind
        // resolveMode / perAxisRoute but can run before any lighting system
        // has bound 28 (Metal re-binds the shadow table per encoder).
        IRRender::createNamedResource<Buffer>(
            "PerAxisWinnerPlaceholder",
            nullptr,
            sizeof(std::uint32_t),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_PerAxisResolveScratch
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

        // Per-axis store list-walk split (#1739). One list buffer holding three
        // axis-keyed regions + one indirect-params buffer holding three dispatch
        // structs. Sized so each region's / struct's byte offset is a multiple of
        // kPerAxisSsboAlignBytes (the portable-safe SSBO bind-range alignment) —
        // these are bound onto 25/26 via bindRange at per-axis dispatch time.
        // Created with the same 25/26 bind indices but restored to the full
        // compact buffers below, so the steady-state binding (single-canvas +
        // every other canvas's compact) is unchanged.
        constexpr int kPerAxisStrideAlignElems =
            kPerAxisSsboAlignBytes / static_cast<int>(sizeof(std::uint32_t));
        const int regionStride =
            IRMath::divCeil(maxSingleVoxels, kPerAxisStrideAlignElems) * kPerAxisStrideAlignElems;
        IRRender::createNamedResource<Buffer>(
            "PerAxisCompactedVoxelIndices",
            nullptr,
            static_cast<size_t>(3) * regionStride * sizeof(std::uint32_t),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_CompactedVoxelIndices
        );
        IRRender::createNamedResource<Buffer>(
            "PerAxisIndirectDispatchParams",
            nullptr,
            static_cast<size_t>(3) * kPerAxisSsboAlignBytes,
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_IndirectDispatchParams
        );

        // Fog-of-war cull placeholder (#2008): a 1×1 all-visible texture bound at
        // image slot 0 of the compact dispatch for any canvas without a real
        // C_CanvasFogOfWar. Seeded visible so it is harmless even if the shader's
        // imageSize<=1 short-circuit were ever removed; in practice the shader
        // never samples it.
        IRRender::createNamedResource<Texture2D>(
            "FogCullVisiblePlaceholder",
            TextureKind::TEXTURE_2D,
            1,
            1,
            TextureFormat::RGBA8,
            TextureWrap::CLAMP_TO_EDGE,
            TextureFilter::NEAREST
        );
        {
            const std::array<std::uint8_t, 4> visiblePixel = {kFogStateVisible, 0u, 0u, 0u};
            IRRender::getNamedResource<Texture2D>("FogCullVisiblePlaceholder")
                ->subImage2D(
                    0,
                    0,
                    1,
                    1,
                    PixelDataFormat::RGBA,
                    PixelDataType::UNSIGNED_BYTE,
                    visiblePixel.data()
                );
        }

        // Analytic vision-circle UBO for the compact's fog cull. Aliases slot 27
        // (kBufferIndex_FogObservers) like FOG_TO_TRIXEL's own observer UBO; bound
        // transiently for the compact dispatch and rebound by LIGHTING_TO_TRIXEL
        // before its own use (the rebind-before-use discipline). Seeded count-0 so
        // a non-fog canvas's compact never reads stale circles.
        IRRender::createNamedResource<Buffer>(
            "FogCullObservers",
            nullptr,
            sizeof(FrameDataFogObservers),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_FogObservers
        );
        {
            const FrameDataFogObservers zeroObservers{};
            IRRender::getNamedResource<Buffer>("FogCullObservers")
                ->subData(0, sizeof(FrameDataFogObservers), &zeroObservers);
        }

        // Chunk-occlusion query buffer (#1294 child 2/3): a 32-byte header record
        // + one record per pool-chunk. Created on slot 25 like the per-axis
        // buffers (bound there transiently by the pre-pass); the restore below
        // returns slot 25 to the full compacted-index buffer for steady state.
        IRRender::createNamedResource<Buffer>(
            "ChunkOcclusionQueryBuffer",
            nullptr,
            static_cast<size_t>(maxVoxelPoolChunks + 1) * sizeof(ChunkOcclusionQuery),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_CompactedVoxelIndices
        );

        SystemId systemId = registerSystem<
            VOXEL_TO_TRIXEL_STAGE_1,
            C_VoxelPool,
            C_TriangleCanvasTextures,
            C_CanvasLocalRotation>("SingleVoxelToCanvasFirst");
        auto *p = getSystemParams<System<VOXEL_TO_TRIXEL_STAGE_1>>(systemId);
        p->compactProgram_ = IRRender::getNamedResource<ShaderProgram>("VoxelCompactProgram");
        p->cellCompactProgram_ =
            IRRender::getNamedResource<ShaderProgram>("PerAxisCellCompactProgram");
        p->cellFinalizeProgram_ =
            IRRender::getNamedResource<ShaderProgram>("PerAxisCellFinalizeProgram");
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
        p->compactedBuf_ = IRRender::getNamedResource<Buffer>("CompactedVoxelIndices");
        p->perAxisCompactedBuf_ =
            IRRender::getNamedResource<Buffer>("PerAxisCompactedVoxelIndices");
        p->perAxisIndirectBuf_ =
            IRRender::getNamedResource<Buffer>("PerAxisIndirectDispatchParams");
        p->perAxisRegionStride_ = regionStride;
        p->occlusionProgram_ = IRRender::getNamedResource<ShaderProgram>("ChunkOcclusionProgram");
        p->chunkOcclusionQueryBuf_ =
            IRRender::getNamedResource<Buffer>("ChunkOcclusionQueryBuffer");
        p->maxPoolChunks_ = maxVoxelPoolChunks;
        p->fogCullPlaceholder_ = IRRender::getNamedResource<Texture2D>("FogCullVisiblePlaceholder");
        p->fogObserverBuf_ = IRRender::getNamedResource<Buffer>("FogCullObservers");
        // The per-axis buffers were created with the 25/26 bind indices, which
        // displaced the full compact buffers' steady-state binding. Restore
        // 25/26 to the full buffers; the per-axis buffers are re-bound onto
        // 25/26 transiently (via bindRange) only inside dispatchPerAxisCanvases.
        p->compactedBuf_->bindBase(
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_CompactedVoxelIndices
        );
        p->indirectBuf_->bindBase(
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_IndirectDispatchParams
        );
        // Intra-tick sub-stage timing (#2280): this system is deliberately NOT
        // tagged for the per-system GpuStageTimingObserver. A single per-tick
        // bracket bundles compact + clear + stage-1 + stage-2 into one opaque
        // `voxelStage1` value (the ~140 ms #2258 could not attribute). Instead
        // the per-canvas tick brackets each of its four dispatch groups with a
        // GpuSubStageScope, filling the `canvasClear` / `voxelCompact` /
        // `voxelStage1` / `voxelStage2` rows individually. Not tagging here
        // leaves the device timestamp attachment slot free for the sub-scopes
        // to reuse during this tick.
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_VOXEL_TO_TRIXEL_H */
