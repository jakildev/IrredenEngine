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

// What syncAllocationToCameraYaw does to the main canvas's per-axis set this
// frame.
enum class LifecycleStep : int {
    KEEP = 0,
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
    int parkedFrames_ = 0;
};

// The lifecycle policy with the ECS lookups and GPU calls lifted out, so it
// runs headlessly; syncAllocationToCameraYaw is its only caller in the engine.
constexpr LifecycleStep lifecycleStep(LifecycleState state) {
    if (state.rotating_) {
        if (state.live_) {
            return LifecycleStep::KEEP;
        }
        if (!state.parked_) {
            return LifecycleStep::ALLOCATE;
        }
        return state.parkedFits_ ? LifecycleStep::UNPARK : LifecycleStep::REPLACE_PARKED;
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

    // The sizes decide between binding the parked set and allocating a new one;
    // a frame that keeps or parks the live set never needs them.
    IRMath::ivec2 size{0, 0};
    IRMath::ivec2 mainSize{0, 0};
    if (rotating && !axes.isAllocated()) {
        auto cardinal =
            IREntity::getComponentOptional<IRComponents::C_TriangleCanvasTextures>(mainCanvas);
        if (!cardinal.has_value()) {
            return;
        }
        mainSize = (*cardinal.value()).size_;
        size = IRMath::perAxisTrixelCanvasWorstCaseSize(mainSize, kMinOnScreenTrixelSizePx);
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
        .parkedFits_ = axes.parked_.fits(size, mainSize),
        .parkedFrames_ = axes.parkedFrames_,
    })) {
    case LifecycleStep::KEEP:
        return;
    case LifecycleStep::ALLOCATE:
        timed(witness.perAxisAllocate_, [&] { axes.allocate(size, mainSize); });
        return;
    case LifecycleStep::UNPARK:
        timed(witness.perAxisUnpark_, [&] { axes.unpark(); });
        return;
    case LifecycleStep::REPLACE_PARKED:
        timed(witness.perAxisRelease_, [&] { axes.parked_.release(); });
        timed(witness.perAxisAllocate_, [&] { axes.allocate(size, mainSize); });
        return;
    case LifecycleStep::PARK:
        timed(witness.perAxisPark_, [&] { axes.park(); });
        return;
    case LifecycleStep::RELEASE_PARKED:
        timed(witness.perAxisRelease_, [&] { axes.parked_.release(); });
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
        (*cardinal.value()).size_,
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
// 1/2/3 axis selector) at the capped lattice density the store wrote,
// and restores the single-canvas state on destruction: route 0, the uncapped
// effSub density, and the voxel-compaction slots 25/26 (see
// restoreVoxelCompactionSlots — the loop below borrows them). One definition
// of the patch/restore discipline those three dispatches each hand-rolled —
// the FrameYawRestoreGuard idiom (system_bake_sun_shadow_map.hpp) applied to
// the lighting family, so a new consumer cannot forget a restore.
class LightingRouteScope {
  public:
    LightingRouteScope(
        IRRender::Buffer *frameDataUbo,
        IRRender::Buffer *&voxelCompactedBuf,
        IRRender::Buffer *&voxelIndirectBuf
    )
        : m_frameDataUbo{frameDataUbo}
        , m_voxelCompactedBuf{voxelCompactedBuf}
        , m_voxelIndirectBuf{voxelIndirectBuf} {
        const int kPerAxisRoute = 1;
        m_frameDataUbo->subData(
            offsetof(IRRender::FrameDataVoxelToCanvas, perAxisRoute_),
            sizeof(int),
            &kPerAxisRoute
        );
        setUboSubdivisionDensity(m_frameDataUbo, subdivisionDensity());
    }

    ~LightingRouteScope() {
        const int kSingleCanvasRoute = 0;
        m_frameDataUbo->subData(
            offsetof(IRRender::FrameDataVoxelToCanvas, perAxisRoute_),
            sizeof(int),
            &kSingleCanvasRoute
        );
        setUboSubdivisionDensity(m_frameDataUbo, IRRender::getVoxelRenderEffectiveSubdivisions());
        restoreVoxelCompactionSlots(m_voxelCompactedBuf, m_voxelIndirectBuf);
    }

    LightingRouteScope(const LightingRouteScope &) = delete;
    LightingRouteScope &operator=(const LightingRouteScope &) = delete;
    LightingRouteScope(LightingRouteScope &&) = delete;
    LightingRouteScope &operator=(LightingRouteScope &&) = delete;

  private:
    IRRender::Buffer *m_frameDataUbo;
    // References to the owning system's lazily-resolved members (the
    // restoreVoxelCompactionSlots contract) — the scope is stack-local inside
    // one tick, so the referents always outlive it.
    IRRender::Buffer *&m_voxelCompactedBuf;
    IRRender::Buffer *&m_voxelIndirectBuf;
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
