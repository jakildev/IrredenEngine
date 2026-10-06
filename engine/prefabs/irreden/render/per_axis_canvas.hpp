#ifndef IR_PREFAB_PER_AXIS_CANVAS_H
#define IR_PREFAB_PER_AXIS_CANVAS_H

// Driver-side lifecycle for the main world canvas's per-axis trixel canvases
// (smooth camera Z-yaw; docs/design/per-axis-trixel-canvas-rotation.md).
// Cross-entity orchestration — look up the main canvas, read the camera yaw,
// allocate, park and release the GPU set — lives here in a prefab-scoped
// namespace rather than on the component (engine/prefabs/CLAUDE.md Pattern B),
// so the C_PerAxisTrixelCanvases layout stays trivial and archetype-iteration
// friendly.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/gpu_stage_timing.hpp>

#include <cstddef>
#include <cstdint>

namespace IRPrefab::PerAxisCanvas {

// Minimum on-screen trixel size (framebuffer px) bounding the skinny-axis
// texture density (see IRMath::perAxisTrixelCanvasWorstCaseSize). ≈1 px is the
// configured balance between seam-free coverage and texture size.
inline constexpr float kMinOnScreenTrixelSizePx = 1.0f;

// Consecutive cardinal frames a parked per-axis set stays resident before it is
// freed: about two seconds at 60 fps. A turn that pauses on a cardinal, or a
// sweep that steps across one, re-enters the per-axis path on the resident set
// instead of re-allocating it; a camera that settles on a cardinal frees the
// memory once the window passes. Longer than any capture suite's settle between
// poses (the canvas_stress suite holds each pose 60 frames, the yaw ramp 16),
// so a suite that alternates cardinal and rotated poses exercises the unpark.
inline constexpr int kParkedCardinalFrames = 120;

// Texel size of each per-axis face store for the current main canvas.
inline IRMath::ivec2 storeSize() {
    return IRMath::perAxisTrixelCanvasWorstCaseSize(
        IRMath::ivec2(IRRender::getMainCanvasSizeTrixels()),
        kMinOnScreenTrixelSizePx
    );
}

// The frame a store of @p size is keyed in this frame
// (FrameDataVoxelToCanvas::perAxisStoreFrame_): the nearest-cardinal view,
// with the window anchored on the world point at the middle of the screen.
// The store pass, every pass that inverts a store cell, and the framebuffer
// scatter each call this from the same camera state.
inline IRMath::ivec4 storeFrame(IRMath::ivec2 size) {
    const float visualYaw = IRPrefab::Camera::getYaw();
    const IRMath::CardinalIndex cardinal =
        IRMath::rasterYawCardinalIndex(IRPrefab::Camera::computeYawSplit(visualYaw).first);
    const IRMath::ivec2 anchor = IRMath::perAxisStoreAnchor(
        IRRender::getEffectiveCameraIso(),
        visualYaw,
        cardinal,
        IRRender::getViewReferenceHeight()
    );
    return IRMath::ivec4(
        IRMath::trixelOriginOffsetZ1(size) + anchor,
        static_cast<int>(cardinal),
        0
    );
}

// What syncAllocationToCameraYaw does to the main canvas's per-axis set this
// frame.
enum class LifecycleStep : int {
    KEEP = 0,
    RESIZE_RESOLVE,
    REPLACE_LIVE,
    UNPARK_RESIZE_RESOLVE,
    ALLOCATE,       // rotating, nothing resident
    UNPARK,         // rotating, the parked set fits the cardinal canvas
    REPLACE_PARKED, // rotating, the parked set was sized for another canvas
    PARK,           // cardinal frame with a live set
    RELEASE_PARKED, // cardinal frame; the parked set has waited out its window
};

struct LifecycleState {
    bool rotating_ = false;
    bool live_ = false;
    bool parked_ = false;
    bool parkedFits_ = false;
    bool liveFits_ = true;
    bool liveResolveFits_ = true;
    bool parkedResolveFits_ = true;
    int parkedFrames_ = 0;
};

// The lifecycle policy with the ECS lookups and GPU calls lifted out, so it
// runs headlessly.
constexpr LifecycleStep lifecycleStep(LifecycleState state) {
    if (state.rotating_) {
        if (state.live_) {
            if (!state.liveFits_) {
                return LifecycleStep::REPLACE_LIVE;
            }
            return state.liveResolveFits_ ? LifecycleStep::KEEP : LifecycleStep::RESIZE_RESOLVE;
        }
        if (!state.parked_) {
            return LifecycleStep::ALLOCATE;
        }
        if (!state.parkedFits_) {
            return LifecycleStep::REPLACE_PARKED;
        }
        return state.parkedResolveFits_ ? LifecycleStep::UNPARK
                                        : LifecycleStep::UNPARK_RESIZE_RESOLVE;
    }
    if (state.live_) {
        return LifecycleStep::PARK;
    }
    if (state.parked_ && state.parkedFrames_ >= kParkedCardinalFrames) {
        return LifecycleStep::RELEASE_PARKED;
    }
    return LifecycleStep::KEEP;
}

// Keep the main canvas's per-axis set in step with the camera: live while the
// camera sits at a non-cardinal residual yaw, parked on a cardinal frame and
// freed after kParkedCardinalFrames of them. Idempotent — safe to call every
// frame; it acts only on the transitions lifecycleStep names. No-op when the
// main canvas has no C_PerAxisTrixelCanvases (e.g. before the renderer is
// wired). Called once per frame from VOXEL_TO_TRIXEL_STAGE_1::beginTick.
inline void syncAllocationToCameraYaw() {
    const IREntity::EntityId mainCanvas = IRRender::getCanvas("main");
    if (mainCanvas == IREntity::kNullEntity) {
        return;
    }
    auto perAxis =
        IREntity::getComponentOptional<IRComponents::C_PerAxisTrixelCanvases>(mainCanvas);
    if (!perAxis.has_value()) {
        return;
    }
    IRComponents::C_PerAxisTrixelCanvases &axes = *perAxis.value();

    const float residualYaw = IRPrefab::Camera::computeYawSplit(IRPrefab::Camera::getYaw()).second;
    // computeYawSplit deadbands the residual to exactly 0 at a settled cardinal
    // (Camera::kResidualYawDeadband), so this `!= 0` allocation gate and
    // the render path-select gate in system_voxel_to_trixel share the identical
    // predicate — they can never disagree about whether the per-axis textures
    // should be live.
    const bool rotating = residualYaw != 0.0f;

    // Face storage covers the logical viewport; the resolve uses the density-scaled backing.
    IRMath::ivec2 size{0, 0};
    IRMath::ivec2 mainSize{0, 0};
    if (rotating) {
        auto cardinal =
            IREntity::getComponentOptional<IRComponents::C_TriangleCanvasTextures>(mainCanvas);
        if (!cardinal.has_value()) {
            return;
        }
        mainSize = (*cardinal.value()).size_;
        size = storeSize();
    }
    if (!rotating && !axes.isAllocated() && axes.hasParked()) {
        ++axes.parkedFrames_;
    }

    IRRender::RenderRunWitness &witness = IRRender::renderRunWitness();
    const auto timed = [](IRRender::CpuPhaseTiming &phase, auto &&step) {
        const IRRender::TimePoint start = IRRender::SteadyClock::now();
        step();
        phase.record(IRRender::elapsedMs(start, IRRender::SteadyClock::now()));
    };
    switch (lifecycleStep({
        .rotating_ = rotating,
        .live_ = axes.isAllocated(),
        .parked_ = axes.hasParked(),
        .parkedFits_ = axes.parked_.size_ == size,
        .liveFits_ = axes.size_ == size,
        .liveResolveFits_ = axes.mainSize_ == mainSize,
        .parkedResolveFits_ = axes.parked_.mainSize_ == mainSize,
        .parkedFrames_ = axes.parkedFrames_,
    })) {
    case LifecycleStep::KEEP:
        return;
    case LifecycleStep::RESIZE_RESOLVE:
        timed(witness.perAxisAllocate_, [&] { axes.resizeResolveDepth(mainSize); });
        return;
    case LifecycleStep::REPLACE_LIVE:
        timed(witness.perAxisRelease_, [&] { axes.release(); });
        timed(witness.perAxisAllocate_, [&] { axes.allocate(size, mainSize); });
        return;
    case LifecycleStep::UNPARK_RESIZE_RESOLVE:
        timed(witness.perAxisUnpark_, [&] { axes.unpark(); });
        timed(witness.perAxisAllocate_, [&] { axes.resizeResolveDepth(mainSize); });
        return;
    case LifecycleStep::ALLOCATE:
        timed(witness.perAxisAllocate_, [&] { axes.allocate(size, mainSize); });
        return;
    case LifecycleStep::UNPARK:
        timed(witness.perAxisUnpark_, [&] { axes.unpark(); });
        return;
    case LifecycleStep::REPLACE_PARKED:
        timed(witness.perAxisRelease_, [&] { axes.releaseParked(); });
        timed(witness.perAxisAllocate_, [&] { axes.allocate(size, mainSize); });
        return;
    case LifecycleStep::PARK:
        timed(witness.perAxisPark_, [&] { axes.park(); });
        return;
    case LifecycleStep::RELEASE_PARKED:
        timed(witness.perAxisRelease_, [&] { axes.releaseParked(); });
        return;
    }
}

// Capped per-axis lattice density (`subPerAxis`) for the smooth-camera-Z-yaw
// store. The per-axis face-local lattice is `world × density`; the
// bounded canvas (perAxisTrixelCanvasWorstCaseSize) does NOT scale with the
// subdivision factor, so a large density drives on-screen cells off the canvas
// and they are silently dropped (the black-hole clip). This caps the density
// to the canvas via IRMath::perAxisSubdivisionCap.
//
// It is a pure function of the main canvas cardinal size + camera zoom + render
// subdivisions, so EVERY per-axis pass — the store, the per-axis AO/lighting
// recovery (perAxisCellToWorld3D reads it via voxelRenderOptions.y), and the
// framebuffer forward-scatter — computes the identical value and the world↔cell
// scale stays consistent. Returns the uncapped effective subdivisions when not
// subdividing (NONE mode) or when the cap doesn't bite.
inline int subdivisionDensity() {
    const int effSub = IRRender::getVoxelRenderEffectiveSubdivisions();
    if (IRRender::getSubdivisionMode() == IRRender::SubdivisionMode::NONE) {
        return effSub;
    }
    const IREntity::EntityId mainCanvas = IRRender::getCanvas("main");
    if (mainCanvas == IREntity::kNullEntity) {
        return effSub;
    }
    auto cardinal =
        IREntity::getComponentOptional<IRComponents::C_TriangleCanvasTextures>(mainCanvas);
    if (!cardinal.has_value()) {
        return effSub;
    }
    const int cap = IRMath::perAxisSubdivisionCap(
        IRMath::ivec2(IRRender::getMainCanvasSizeTrixels()),
        IRRender::getCameraZoom(),
        kMinOnScreenTrixelSizePx
    );
    return IRMath::clamp(effSub, 1, cap);
}

// Patch the shared voxel frame-data UBO's `voxelRenderOptions_.y` (the per-axis
// `subPerAxis` density). Pass-scoped: a per-axis dispatch sets the capped
// density before its loop and restores the uncapped `effSub` after, mirroring
// the existing per-axis `perAxisRoute_` set/restore. AO / lighting / sun-shadow
// only `subData` `perAxisRoute_`, so they reuse the UBO's `voxelRenderOptions_`
// and must patch the density here for their face-local world recovery to match
// the capped store.
inline void setUboSubdivisionDensity(IRRender::Buffer *frameDataUbo, int density) {
    frameDataUbo->subData(
        offsetof(IRRender::FrameDataVoxelToCanvas, voxelRenderOptions_) + sizeof(int),
        sizeof(int),
        &density
    );
}

// Rebind SSBO slots 25/26 (kBufferIndex_PerAxisCellCompacted/Indirect) to
// VOXEL_TO_TRIXEL_STAGE_1's single-canvas voxel-compaction buffers after a
// per-axis compute dispatch has borrowed them for its own cell list. Every
// per-axis consumer (AO, sun shadow, lighting, screen-depth
// resolve, the framebuffer scatter) binds these slots to its
// C_PerAxisTrixelCanvases-owned buffers via bindRange; leaving them bound
// past the dispatch means the next frame's STAGE_1 compact — which binds
// its own buffers once at create() and trusts sticky global state
// thereafter — silently re-reads the cell list as the voxel index list and
// corrupts world voxels (the center-cube regression). Callers own
// the lazy-resolved pointer pair as members (mirrors the member-on-System
// caching pattern) and pass them by reference so the named-resource lookup
// only runs once per system.
inline void restoreVoxelCompactionSlots(
    IRRender::Buffer *&voxelCompactedBuf, IRRender::Buffer *&voxelIndirectBuf
) {
    if (voxelCompactedBuf == nullptr) {
        voxelCompactedBuf = IRRender::getNamedResource<IRRender::Buffer>("CompactedVoxelIndices");
    }
    if (voxelIndirectBuf == nullptr) {
        voxelIndirectBuf = IRRender::getNamedResource<IRRender::Buffer>("IndirectDispatchParams");
    }
    // Unguarded: getNamedResource asserts on a miss rather than returning null
    // and VOXEL_TO_TRIXEL_STAGE_1, which creates both buffers,
    // is a hard precondition of every per-axis consumer.
    voxelCompactedBuf->bindBase(
        IRRender::BufferTarget::SHADER_STORAGE,
        IRRender::kBufferIndex_PerAxisCellCompacted
    );
    voxelIndirectBuf->bindBase(
        IRRender::BufferTarget::SHADER_STORAGE,
        IRRender::kBufferIndex_PerAxisCellIndirect
    );
}

// RAII scope for the per-axis lighting-family dispatches (AO / sun-shadow /
// lighting): flips the shared voxel frame-data UBO onto the per-axis decode
// route (perAxisRoute_ = 1 — a boolean route flag on the lighting path; the
// shader recovers the axis per-pixel from faceId, distinct from stage-1's
// 1/2/3 axis selector) at the capped lattice density the store wrote, with
// canvasSizePixels_ set to the per-axis store size — the overflow-entry kernels
// derive the store's origin anchor from it, and the cell kernels read their
// bound image's own size instead. Restores the single-canvas state on
// destruction: route 0, the uncapped effSub density, the main canvas size, and
// the voxel-compaction slots 25/26 (see restoreVoxelCompactionSlots — the loop
// below borrows them). One definition
// of the patch/restore discipline those three dispatches each hand-rolled —
// the FrameYawRestoreGuard idiom (system_bake_sun_shadow_map.hpp) applied to
// the lighting family, so a new consumer cannot forget a restore.
class LightingRouteScope {
  public:
    LightingRouteScope(
        IRRender::Buffer *frameDataUbo,
        IRRender::Buffer *&voxelCompactedBuf,
        IRRender::Buffer *&voxelIndirectBuf,
        IRMath::ivec2 storeCanvasSize,
        IRMath::ivec2 mainCanvasSize
    )
        : m_frameDataUbo{frameDataUbo}
        , m_voxelCompactedBuf{voxelCompactedBuf}
        , m_voxelIndirectBuf{voxelIndirectBuf}
        , m_mainCanvasSize{mainCanvasSize} {
        const int kPerAxisRoute = 1;
        m_frameDataUbo->subData(
            offsetof(IRRender::FrameDataVoxelToCanvas, perAxisRoute_),
            sizeof(int),
            &kPerAxisRoute
        );
        setUboSubdivisionDensity(m_frameDataUbo, subdivisionDensity());
        setUboCanvasSize(storeCanvasSize);
    }

    ~LightingRouteScope() {
        const int kSingleCanvasRoute = 0;
        m_frameDataUbo->subData(
            offsetof(IRRender::FrameDataVoxelToCanvas, perAxisRoute_),
            sizeof(int),
            &kSingleCanvasRoute
        );
        setUboSubdivisionDensity(m_frameDataUbo, IRRender::getVoxelRenderEffectiveSubdivisions());
        setUboCanvasSize(m_mainCanvasSize);
        restoreVoxelCompactionSlots(m_voxelCompactedBuf, m_voxelIndirectBuf);
    }

    LightingRouteScope(const LightingRouteScope &) = delete;
    LightingRouteScope &operator=(const LightingRouteScope &) = delete;
    LightingRouteScope(LightingRouteScope &&) = delete;
    LightingRouteScope &operator=(LightingRouteScope &&) = delete;

  private:
    void setUboCanvasSize(IRMath::ivec2 size) {
        m_frameDataUbo->subData(
            offsetof(IRRender::FrameDataVoxelToCanvas, canvasSizePixels_),
            sizeof(IRMath::ivec2),
            &size
        );
    }

    IRRender::Buffer *m_frameDataUbo;
    // References to the owning system's lazily-resolved members (the
    // restoreVoxelCompactionSlots contract) — the scope is stack-local inside
    // one tick, so the referents always outlive it.
    IRRender::Buffer *&m_voxelCompactedBuf;
    IRRender::Buffer *&m_voxelIndirectBuf;
    IRMath::ivec2 m_mainCanvasSize;
};

// One indirect compute dispatch per axis over that axis's compacted OCCUPIED
// cell list: calls @p bindAxis(axis) for the pass-specific image
// bindings, binds the axis's region of the component-owned compacted-cell +
// dispatch-args buffers onto slots 25/26 (borrowing them — the caller restores
// via restoreVoxelCompactionSlots / LightingRouteScope), then issues the
// indirect dispatch from the axis's args region. One definition of the loop
// the per-axis AO / sun-shadow / lighting / screen-depth-resolve dispatches
// each hand-rolled.
template <typename BindAxis>
inline void dispatchPerAxisCells(IRComponents::C_PerAxisTrixelCanvases &axes, BindAxis &&bindAxis) {
    IRRender::Buffer *cellCompacted = axes.cellCompacted_.second;
    IRRender::Buffer *cellIndirect = axes.cellIndirect_.second;
    const int regionStride = axes.cellRegionStride_;
    for (int axis = 0; axis < IRComponents::C_PerAxisTrixelCanvases::kAxisCount; ++axis) {
        bindAxis(axis);
        cellCompacted->bindRange(
            IRRender::BufferTarget::SHADER_STORAGE,
            IRRender::kBufferIndex_PerAxisCellCompacted,
            static_cast<std::ptrdiff_t>(axis) * regionStride *
                static_cast<int>(sizeof(std::uint32_t)),
            static_cast<size_t>(regionStride) * sizeof(std::uint32_t)
        );
        cellIndirect->bindRange(
            IRRender::BufferTarget::SHADER_STORAGE,
            IRRender::kBufferIndex_PerAxisCellIndirect,
            static_cast<std::ptrdiff_t>(axis) * IRRender::kPerAxisCellIndirectStrideBytes,
            IRRender::kPerAxisCellIndirectStrideBytes
        );
        IRRender::device()->dispatchComputeIndirect(
            cellIndirect,
            static_cast<std::ptrdiff_t>(axis) * IRRender::kPerAxisCellIndirectStrideBytes +
                IRRender::kPerAxisCellDispatchArgsOffsetBytes
        );
    }
}

} // namespace IRPrefab::PerAxisCanvas

#endif /* IR_PREFAB_PER_AXIS_CANVAS_H */
