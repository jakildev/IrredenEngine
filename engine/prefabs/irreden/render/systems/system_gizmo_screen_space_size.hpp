#ifndef SYSTEM_GIZMO_SCREEN_SPACE_SIZE_H
#define SYSTEM_GIZMO_SCREEN_SPACE_SIZE_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/render/components/component_gizmo_handle.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

namespace IRSystem {

// UPDATE-phase system that keeps editor gizmos at a constant pixel size
// regardless of camera zoom (T-164 Phase 2). Each tick:
//
//   scale = pixelSize_ / zoomSnapshot_
//   shape.params_ = handle.referenceParams_ * scale
//   if (!handle.isAnchor_) {
//       localTransform.translation_ = handle.referenceLocalPos_ * scale
//   }
//
// Writes are derived from the per-handle `referenceParams_` /
// `referenceLocalPos_` (set once by the Phase 1 builders), so successive
// frames don't compound — the same baseline maps to the same on-screen
// pixel size across the full zoom range.
//
// `isAnchor_` distinguishes single-entity gizmo markers (joint, IK) whose
// own `C_LocalTransform` IS the world-space anchor (the editor writes it
// post-construction) from child handles parented under a group root.
// For anchors we scale only `params_`; touching `translation_` would
// clobber the editor's placement.
//
// `pixelSize_` is a per-system knob the caller can tune. `1.0f` means
// the handle renders at its Phase 1 world-space size when zoom == 1.0;
// raising it makes every handle proportionally bigger in pixels.
//
// Must run BEFORE `PROPAGATE_TRANSFORM` in the UPDATE pipeline so the
// rescaled local translations propagate into `C_WorldTransform` the same
// frame; `SHAPES_TO_TRIXEL` reads `C_WorldTransform` in RENDER.
//
// Z-yaw safety: the rescaled `C_ShapeDescriptor::params_` is consumed
// by `system_shapes_to_trixel` AFTER the rotate snapshot, so the
// shapes shader sees the new scaled extent at the same cardinal yaw
// the rest of the frame uses — no special yaw handling required here.
template <> struct System<GIZMO_SCREEN_SPACE_SIZE> {
    float pixelSize_ = 1.0f;
    float zoomSnapshot_ = 1.0f;
    float scaleSnapshot_ = 1.0f;

    void beginTick() {
        const float zoom = IRRender::getCameraZoom().x;
        // Camera zoom is set by IRRender; the editor's mouse-wheel handler
        // is the only mutator. Guard against pathological values so a
        // misconfigured creation never blows the gizmo extents up to
        // infinity (would trip the cull bounds and crash render).
        zoomSnapshot_ = IRMath::max(zoom, 1e-3f);
        scaleSnapshot_ = pixelSize_ / zoomSnapshot_;
    }

    void tick(
        IRComponents::C_GizmoHandle &handle,
        IRComponents::C_ShapeDescriptor &shape,
        IRComponents::C_LocalTransform &localTransform
    ) {
        shape.params_ = handle.referenceParams_ * scaleSnapshot_;
        if (!handle.isAnchor_) {
            localTransform.translation_ = handle.referenceLocalPos_ * scaleSnapshot_;
        }
    }

    static SystemId create() {
        return registerSystem<
            GIZMO_SCREEN_SPACE_SIZE,
            IRComponents::C_GizmoHandle,
            IRComponents::C_ShapeDescriptor,
            IRComponents::C_LocalTransform>("GizmoScreenSpaceSize");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_GIZMO_SCREEN_SPACE_SIZE_H */
