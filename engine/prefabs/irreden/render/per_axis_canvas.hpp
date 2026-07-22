#ifndef IR_PREFAB_PER_AXIS_CANVAS_H
#define IR_PREFAB_PER_AXIS_CANVAS_H

// Driver-side lifecycle for the main world canvas's per-axis trixel canvases
// (smooth camera Z-yaw, #1308; docs/design/per-axis-trixel-canvas-rotation.md).
// Cross-entity orchestration — look up the main canvas, read the camera yaw,
// allocate/release the GPU textures — lives here in a prefab-scoped namespace
// rather than on the component (engine/prefabs/CLAUDE.md Pattern B), so the
// C_PerAxisTrixelCanvases layout stays trivial and archetype-iteration friendly.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>

#include <cstddef>
#include <cstdint>

namespace IRPrefab::PerAxisCanvas {

// Minimum on-screen trixel size (framebuffer px) bounding the skinny-axis
// texture density (see IRMath::perAxisTrixelCanvasWorstCaseSize). ≈1 px is the
// starting point from the design doc's open decisions; tune against seam-free
// coverage vs texture size once Stage-1 routing (T2) and the framebuffer passes
// (T3) land.
inline constexpr float kMinOnScreenTrixelSizePx = 1.0f;

namespace detail {
// Allocate a canvas's three per-axis texture sets at the worst-case size for
// its cardinal trixel canvas, plus the screen-space resolve-depth texture at
// the cardinal size (#1453). Used by the camera-yaw (main canvas) allocation
// gate to size the per-axis textures. No-op if already allocated
// (C_PerAxisTrixelCanvases::allocate guards it).
inline void allocatePerAxisForCanvas(
    IRComponents::C_PerAxisTrixelCanvases &axes,
    const IRComponents::C_TriangleCanvasTextures &cardinal
) {
    axes.allocate(
        IRMath::perAxisTrixelCanvasWorstCaseSize(cardinal.size_, kMinOnScreenTrixelSizePx),
        cardinal.size_
    );
}
} // namespace detail

// Allocate the main canvas's per-axis trixel textures while the camera sits at
// a non-cardinal residual yaw, and release them at a cardinal. Idempotent —
// safe to call every frame; it only allocates / frees on the rotation-start /
// rotation-stop transitions. No-op when the main canvas has no
// C_PerAxisTrixelCanvases (e.g. before the renderer is wired). Called once per
// frame from VOXEL_TO_TRIXEL_STAGE_1::beginTick.
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
    // (Camera::kResidualYawDeadband, #1882), so this `!= 0` allocation gate and
    // the render path-select gate in system_voxel_to_trixel share the identical
    // predicate — they can never disagree about whether the per-axis textures
    // should be live.
    const bool rotating = residualYaw != 0.0f;

    if (rotating == axes.isAllocated()) {
        return; // already in the desired allocation state
    }
    if (rotating) {
        auto cardinal =
            IREntity::getComponentOptional<IRComponents::C_TriangleCanvasTextures>(mainCanvas);
        if (!cardinal.has_value()) {
            return;
        }
        detail::allocatePerAxisForCanvas(axes, *cardinal.value());
    } else {
        axes.release();
    }
}

// Capped per-axis lattice density (`subPerAxis`) for the smooth-camera-Z-yaw
// store (#1431). The per-axis face-local lattice is `world × density`; the
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
// per-axis compute dispatch has borrowed them for its own cell list (#1961,
// #2256). Every per-axis consumer (AO, sun shadow, lighting, screen-depth
// resolve, the framebuffer scatter) binds these slots to its
// C_PerAxisTrixelCanvases-owned buffers via bindRange; leaving them bound
// past the dispatch means the next frame's STAGE_1 compact — which binds
// its own buffers once at create() and trusts sticky global state
// thereafter — silently re-reads the cell list as the voxel index list and
// corrupts world voxels (the #1961 center-cube regression). Callers own
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
    if (voxelCompactedBuf != nullptr) {
        voxelCompactedBuf->bindBase(
            IRRender::BufferTarget::SHADER_STORAGE,
            IRRender::kBufferIndex_PerAxisCellCompacted
        );
    }
    if (voxelIndirectBuf != nullptr) {
        voxelIndirectBuf->bindBase(
            IRRender::BufferTarget::SHADER_STORAGE,
            IRRender::kBufferIndex_PerAxisCellIndirect
        );
    }
}

// RAII scope for the per-axis lighting-family dispatches (AO / sun-shadow /
// lighting): flips the shared voxel frame-data UBO onto the per-axis decode
// route (perAxisRoute_ = 1 — a boolean route flag on the lighting path; the
// shader recovers the axis per-pixel from faceId, distinct from stage-1's
// 1/2/3 axis selector) at the #1431-capped lattice density the store wrote,
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
// cell list (#2256): calls @p bindAxis(axis) for the pass-specific image
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
