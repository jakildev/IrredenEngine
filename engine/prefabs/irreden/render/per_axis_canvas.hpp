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

    const float residualYaw =
        IRPrefab::Camera::computeYawSplit(IRPrefab::Camera::getYaw()).second;
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
        axes.allocate(IRMath::perAxisTrixelCanvasWorstCaseSize(
            (*cardinal.value()).size_,
            kMinOnScreenTrixelSizePx
        ));
    } else {
        axes.release();
    }
}

} // namespace IRPrefab::PerAxisCanvas

#endif /* IR_PREFAB_PER_AXIS_CANVAS_H */
