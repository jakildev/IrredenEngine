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
#include <irreden/render/voxel_dispatch_grid.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>

#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
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

inline void buildVoxelFrameData(
    FrameDataVoxelToCanvas &frameData,
    const C_TriangleCanvasTextures &canvas,
    int liveVoxelCount,
    const C_CanvasLocalRotation &canvasRotation
) {
    // The single-canvas raster always uploads perAxisRoute_ == 0 (byte-
    // identical to master). The smooth-Z-yaw per-axis pass flips it to 1/2/3
    // locally per axis and restores it afterward (see dispatchPerAxisCanvases).
    frameData.perAxisRoute_ = 0;

    const auto renderMode = IRRender::getSubdivisionMode();
    const int effectiveSubdivisions = IRRender::getVoxelRenderEffectiveSubdivisions();
    const ivec2 dispatchGrid = voxelDispatchGridForCount(liveVoxelCount);

    frameData.cameraTrixelOffset_ = IRRender::getEffectiveCameraIso();
    frameData.trixelCanvasOffsetZ1_ = IRMath::trixelOriginOffsetZ1(canvas.size_);
    frameData.voxelRenderOptions_ = ivec2(static_cast<int>(renderMode), effectiveSubdivisions);
    frameData.voxelDispatchGrid_ = dispatchGrid;
    frameData.voxelCount_ = liveVoxelCount;
    frameData.canvasSizePixels_ = canvas.size_;
    // Per-voxel occlusion depth axis (#1462). World canvas + the smooth-Z-yaw
    // per-axis route keep the fixed (1,1,1) iso depth axis (byte-identical
    // x+y+z); the detached branch below overrides it with the entity-rotated
    // axis. frameData_ is a reused member, so this must be reset every frame so
    // a prior detached canvas's axis can't leak into a world frame.
    frameData.voxelDepthAxis_ = vec4(1.0f, 1.0f, 1.0f, 0.0f);

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
        // Per-entity SO(3) visible triplet: the three faces the camera
        // actually sees for this entity's orientation (one per axis, in
        // X/Y/Z slot order). Previously hardcoded to {X_NEG, Y_NEG, Z_NEG}
        // regardless of rotation, so the snap+residual deform below ran on
        // back-facing faces and entities glitched instead of rotating
        // (#1386). At identity the resolver returns the same legacy triplet,
        // so non-rotating entities stay byte-identical. The resolver is
        // reused verbatim by the main-canvas per-entity path (#1299).
        const std::array<IRMath::FaceId, 3> visibleFaces =
            IRMath::visibleTriplet(canvasRotation.rotation_);
        frameData.visibleFaceIds_ = ivec4(
            static_cast<int>(visibleFaces[0]),
            static_cast<int>(visibleFaces[1]),
            static_cast<int>(visibleFaces[2]),
            0
        );
        // Per-voxel occlusion depth projects onto the entity-rotated iso axis
        // `R⁻¹·(1,1,1)` (#1462) — the same model-frame axis `visibleTriplet`
        // selects faces from, so face visibility and occlusion order stay on
        // one per-entity frame. Identity entity → (1,1,1) → byte-identical.
        frameData.voxelDepthAxis_ = vec4(IRMath::isoDepthAxisModel(canvasRotation.rotation_), 0.0f);
        // Snap to the nearest of the 24 cube orientations and deform by the
        // residual only: a cube is invariant under the snap, so this keeps
        // the per-face skew small enough to stay clean (T-295).
        const vec4 residual = IRMath::octahedralSnapResidual(canvasRotation.rotation_);
        const mat2 fdX = IRMath::faceDeformationMatrixSO3(IRMath::kXFace, residual);
        const mat2 fdY = IRMath::faceDeformationMatrixSO3(IRMath::kYFace, residual);
        const mat2 fdZ = IRMath::faceDeformationMatrixSO3(IRMath::kZFace, residual);
        // Per-slot upload: slot 0 / 1 / 2 carries the X / Y / Z axis face
        // matrix. `visibleTriplet` returns faces in axis order, so each
        // slot's axis is fixed regardless of polarity — the deform is
        // axis-only (X_NEG and X_POS share the X matrix), so it is unchanged.
        frameData.faceDeform_[0] = vec4(fdX[0], fdX[1]);
        frameData.faceDeform_[1] = vec4(fdY[0], fdY[1]);
        frameData.faceDeform_[2] = vec4(fdZ[0], fdZ[1]);
        return;
    }

    // Main world canvas: rasterYaw picks the integer trixel basis permutation
    // (T-055); residualYaw is folded into faceDeform_[] which the trixel emit
    // shader applies to each sub-pixel offset in 2D iso space (T-293, replaces
    // the T-058 / T-322 screen-space bilinear residual composite). At every
    // non-zero cardinal the WORLD face whose iso footprint lands in each
    // diamond slot rotates with the camera — `visibleFaceIds_` carries the
    // current slot ↔ FaceId map (#1278).
    frameData.visualYaw_ = IRPrefab::Camera::getYaw();
    const auto [rasterYaw, residualYaw] = IRPrefab::Camera::computeYawSplit(frameData.visualYaw_);
    frameData.rasterYaw_ = rasterYaw;
    frameData.residualYaw_ = residualYaw;
    const auto cardinalIndex = IRMath::rasterYawCardinalIndex(rasterYaw);
    const auto visibleFaces = IRMath::visibleFaceTripletCardinal(cardinalIndex);
    frameData.visibleFaceIds_ = ivec4(
        static_cast<int>(visibleFaces[0]),
        static_cast<int>(visibleFaces[1]),
        static_cast<int>(visibleFaces[2]),
        0
    );
    // Per-slot deformation (axis-only; X_NEG and X_POS share the X-axis
    // matrix). At cardinal 0 the per-slot order {X_NEG, Y_NEG, Z_NEG}
    // collapses to the legacy axis order {kXFace, kYFace, kZFace}, so the
    // upload is bit-identical to pre-#1278 master.
    const mat2 fd0 = IRMath::faceDeformationMatrix(visibleFaces[0], residualYaw);
    const mat2 fd1 = IRMath::faceDeformationMatrix(visibleFaces[1], residualYaw);
    const mat2 fd2 = IRMath::faceDeformationMatrix(visibleFaces[2], residualYaw);
    frameData.faceDeform_[0] = vec4(fd0[0], fd0[1]);
    frameData.faceDeform_[1] = vec4(fd1[0], fd1[1]);
    frameData.faceDeform_[2] = vec4(fd2[0], fd2[1]);
}

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
        const auto &uploadMask = buildChunkVisibilityMask(
            voxelPool,
            chunkVp,
            chunkCardinal,
            rotating,
            frameData_.visualYaw_
        );
        chunkVisBuf_->subData(0, uploadMask.size() * sizeof(std::uint32_t), uploadMask.data());

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
            if (lastUploadedCanvas_ != entity) {
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

        // Smooth camera Z-yaw (T3 / #1310): while the main canvas's per-axis
        // canvases are active, SKIP the single-canvas voxel rasterization. The
        // per-axis dispatch below writes the voxels (smooth), and the
        // framebuffer scatter composites them; rasterizing the cardinal-snapped
        // voxels into the single canvas too would double-draw them (the single
        // canvas is composited for its SDF / overlay content, which sits at the
        // SAME world depth as the smooth copies → snapped ghosts). Skipping
        // leaves the single canvas holding only SHAPES_TO_TRIXEL / text /
        // overlay content, which the scatter path composites alongside the
        // smooth voxels. The compact pass above still runs (the per-axis
        // dispatch reuses its compacted-voxel list).
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
