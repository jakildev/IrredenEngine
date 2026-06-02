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
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>

#include <cstddef>
#include <utility>
#include <vector>

namespace IRPrefab::PerAxisCanvas {

// Minimum on-screen trixel size (framebuffer px) bounding the skinny-axis
// texture density (see IRMath::perAxisTrixelCanvasWorstCaseSize). ≈1 px is the
// starting point from the design doc's open decisions; tune against seam-free
// coverage vs texture size once Stage-1 routing (T2) and the framebuffer passes
// (T3) land.
inline constexpr float kMinOnScreenTrixelSizePx = 1.0f;

// Residual-yaw deadband for the allocation gate. |residualYaw| at or below this
// counts as "cardinal" (not rotating): the per-axis textures stay released and
// the renderer stays on the byte-identical single-canvas fast path. A small
// deadband avoids allocate/release churn from float noise right at a cardinal.
inline constexpr float kResidualYawDeadband = 1e-4f;

// Memory bound for the per-DETACHED-entity per-axis trixel canvases (#1463). At
// most this many detached entities hold their textures allocated at once — each
// allocation is 3 axis canvases x 5 textures (color / distance / entity-id /
// AO / sun-shadow) sized to that entity's worst-case trixel canvas, so the peak
// GPU cost of the smooth-detached-rotation path scales with this cap, not with
// however many entities spin at once. Entities past the cap keep rendering
// through the single-canvas octahedral-snap + faceDeformationMatrixSO3 path
// (graceful degradation — blockier off-snap deform, never a crash or a leak).
inline constexpr int kMaxDetachedRotatingCanvases = 8;

// Residual-rotation deadband for the detached allocation gate, expressed as the
// octahedral-snap residual quaternion's vector-part magnitude (= sin(half the
// residual angle), 0 at a snap). At or below this the entity sits on one of the
// 24 octahedral orientations where the single-canvas face-deform is already
// exact, so its per-axis textures stay released and the detached raster stays
// byte-identical. The small deadband avoids allocate/release churn from float
// noise right at a snap — the SO(3) analogue of kResidualYawDeadband.
inline constexpr float kDetachedResidualDeadband = 1e-4f;

namespace detail {
// Allocate a canvas's three per-axis texture sets at the worst-case size for
// its cardinal trixel canvas, plus the screen-space resolve-depth texture at
// the cardinal size (#1453). Shared by the camera-yaw (main canvas) and
// per-entity SO(3) (detached) allocation gates so both size the per-axis
// textures identically. No-op if already allocated (C_PerAxisTrixelCanvases::
// allocate guards it).
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
    const bool rotating = IRMath::abs(residualYaw) > kResidualYawDeadband;

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
        (*cardinal.value()).size_, IRRender::getCameraZoom(), kMinOnScreenTrixelSizePx
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

// Allocate per-axis trixel textures for DETACHED entities currently rotating
// off an octahedral snap, releasing them at a snap. Idempotent and once-per-
// frame; only transitions on rotation start/stop. Bounded by
// kMaxDetachedRotatingCanvases — entities encountered first in archetype-
// iteration order win the budget; the overflow keeps the single-canvas
// octahedral-snap path. Skips non-detached canvases: the main world canvas
// matches this archetype too (every voxel-pool canvas now carries
// C_PerAxisTrixelCanvases) but keeps the C_CanvasLocalRotation sentinel, so
// isDetached() is false and it stays owned by syncAllocationToCameraYaw().
//
// When @p allocatedOut is non-null, it is filled (cleared first) with one
// {canvasEntity, &axes} pair per detached canvas left ALLOCATED this frame —
// the set the P3 forward-scatter store (#1464) routes into. Folding the
// collection into this same scan keeps the per-frame archetype walk single-pass:
// the consumer (VOXEL_TO_TRIXEL_STAGE_1) reuses the result by canvas entity in
// its per-entity tick instead of re-querying the same archetype. The pointers
// are column addresses, valid only for the rest of this pipeline pass (no
// structural change runs before the per-entity ticks consume them).
//
// (#1463) stood the textures up as infrastructure; (#1464, P3a) is the first
// consumer — the detached per-axis face-local store. Called once per frame from
// VOXEL_TO_TRIXEL_STAGE_1::beginTick.
inline void syncAllocationToDetachedEntities(
    std::vector<std::pair<IREntity::EntityId, IRComponents::C_PerAxisTrixelCanvases *>>
        *allocatedOut = nullptr
) {
    if (allocatedOut != nullptr) {
        allocatedOut->clear();
    }

    // Dense per-archetype-column iteration (no per-entity getComponent): the
    // three components arrive as parallel column vectors indexed by row.
    const std::vector<IREntity::ArchetypeNode *> nodes = IREntity::queryArchetypeNodesSimple(
        IREntity::getArchetype<
            IRComponents::C_CanvasLocalRotation,
            IRComponents::C_PerAxisTrixelCanvases,
            IRComponents::C_TriangleCanvasTextures>()
    );

    int allocatedRotating = 0;
    for (IREntity::ArchetypeNode *node : nodes) {
        std::vector<IRComponents::C_CanvasLocalRotation> &rotations =
            IREntity::getComponentData<IRComponents::C_CanvasLocalRotation>(node);
        std::vector<IRComponents::C_PerAxisTrixelCanvases> &axesColumn =
            IREntity::getComponentData<IRComponents::C_PerAxisTrixelCanvases>(node);
        std::vector<IRComponents::C_TriangleCanvasTextures> &cardinals =
            IREntity::getComponentData<IRComponents::C_TriangleCanvasTextures>(node);

        for (int i = 0; i < node->length_; ++i) {
            // The main world canvas (sentinel rotation) is not detached — leave
            // it to syncAllocationToCameraYaw().
            if (!rotations[i].isDetached()) {
                continue;
            }
            IRComponents::C_PerAxisTrixelCanvases &axes = axesColumn[i];

            // Off-snap iff the octahedral-snap residual has a non-trivial
            // rotation: its vector-part magnitude is sin(residualAngle/2), zero
            // at a snap.
            const IRMath::vec4 residual = IRMath::octahedralSnapResidual(rotations[i].rotation_);
            const float residualMagnitude =
                IRMath::length(IRMath::vec3(residual.x, residual.y, residual.z));
            const bool wantAllocated = residualMagnitude > kDetachedResidualDeadband &&
                                       allocatedRotating < kMaxDetachedRotatingCanvases;
            if (wantAllocated) {
                ++allocatedRotating; // counts whether kept from last frame or newly allocated
                // wantAllocated == the post-sync allocation state (the block
                // below only transitions a row that isn't already there), so
                // collect here — before the already-in-state early-out — to
                // catch rows kept allocated from the prior frame too.
                if (allocatedOut != nullptr) {
                    allocatedOut->emplace_back(node->entities_[i], &axes);
                }
            }

            if (wantAllocated == axes.isAllocated()) {
                continue; // already in the desired allocation state
            }
            if (wantAllocated) {
                detail::allocatePerAxisForCanvas(axes, cardinals[i]);
            } else {
                axes.release();
            }
        }
    }
}

} // namespace IRPrefab::PerAxisCanvas

#endif /* IR_PREFAB_PER_AXIS_CANVAS_H */
