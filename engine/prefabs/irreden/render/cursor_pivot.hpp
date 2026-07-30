#ifndef IR_PREFAB_CURSOR_PIVOT_H
#define IR_PREFAB_CURSOR_PIVOT_H

// Cursor-latched camera Z-yaw pivot (#2548, epic #2544 Phase 4) — the two
// pieces `System<CAMERA_MOUSE_ROTATE>`'s Ctrl+Shift+middle-drag needs beyond
// the raw `IRRender::setRotationPivotFocus` call:
//
//   1. `resolveFocusWorld` — the world point under the cursor at its TRUE
//      surface depth, via `IRPrefab::Picking::castVoxelRay`. The pre-#2548
//      latch used `mouseWorldPos3DAtIsoDepth(0)`, an iso-depth-0 point on the
//      cursor ray: for elevated content that lands well behind the clicked
//      surface, so the scene arcs about a point the user never picked (the
//      same wrong-depth class #2547 fixed for the DEFAULT pivot).
//   2. `createIndicator` / `showIndicator` / `hideIndicator` — the marker
//      entity that shows where the pivot actually latched, so the mode is
//      debuggable without guessing.
//
// Prefab-scoped (Pattern B, `engine/prefabs/irreden/render/CLAUDE.md`
// §"Exposing system public API from the prefab layer") — cursor-pivot is a
// per-creation interaction feature, so none of it belongs on `RenderManager`.
//
// The latch is acquired once, on mouse-DOWN, and held for the drag (the #1352
// open question — a pivot that re-follows the live cursor feeds back into the
// rotation it controls). Callers own that policy; this header only resolves a
// point and draws a marker at it.
//
// Contract note (epic #2544 ledger D3): CPU picking never applied the #2545
// raster anchor shift, so `castVoxelRay` already agrees with the raster at
// every cardinal — do NOT add a picking compensation here.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/picking.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

namespace IRPrefab::CursorPivot {

// Marker sizing/color. Deliberately smaller than a gizmo handle — the pivot
// point is a reference mark, not something you grab.
constexpr float kIndicatorRadius = 0.9f;
constexpr IRMath::Color kIndicatorColor{255, 208, 64, 255};

// The world point a cursor-pivot drag should rotate about: the surface the
// cursor is over, at the depth that surface actually renders at.
//
// On a background click there is no surface to latch, and the drag falls back
// to @ref IRRender::getDefaultRotationPivotFocus — i.e. the click behaves
// exactly like the DEFAULT (no-Shift) pivot, which is itself depth-derived and
// already gated by `pivot-verify`'s default-pivot blocks. The alternative
// (`mouseWorldPos3DAtIsoDepth(0)`, the old behavior, kept under the cursor)
// would pin an arbitrary depth on the cursor ray, which is the defect this
// task exists to remove.
//
// @p excludeEntity is skipped by the ray — pass the indicator from a previous
// drag so the marker cannot catch its own ray.
inline IRMath::vec3 resolveFocusWorld(IREntity::EntityId excludeEntity = IREntity::kNullEntity) {
    const auto hit = Picking::castVoxelRay(excludeEntity);
    if (hit.has_value()) {
        return hit->worldHitPos_;
    }
    return IRRender::getDefaultRotationPivotFocus();
}

// Spawn the latched-pivot marker, hidden. Deliberately NOT built through
// `IRPrefab::Gizmo::spawnHandle`, which draws the same SPHERE with the same
// xray flag: that builder also attaches `C_GizmoHandle`, which would put the
// marker in the `GIZMO_HOVER` / `GIZMO_DRAG` archetypes and make a pivot
// reference mark hoverable and draggable.
//
// Rendered by `SHAPES_TO_TRIXEL` through the ordinary SDF path —
// `SHAPE_FLAG_XRAY_OCCLUDED` (a behavior-named flag, per
// engine/render/CLAUDE.md §"Name identifiers after the rendering effect")
// keeps it readable as a faint silhouette when the latched point sits behind
// closer geometry, which is the common case for a surface pick on a concave
// scene.
//
// Call this lazily, on the first cursor-pivot drag rather than at system
// registration: a creation that never uses the mode then spawns no entity, so
// its entity-id layout and every existing capture stay untouched.
inline IREntity::EntityId createIndicator() {
    // SPHERE takes its radius on all three axes (the `C_ShapeDescriptor` param
    // convention) — a radius in `.x` alone is a degenerate shape that renders
    // no pixels.
    IRComponents::C_ShapeDescriptor marker{
        IRRender::ShapeType::SPHERE,
        IRMath::vec4(kIndicatorRadius, kIndicatorRadius, kIndicatorRadius, 0.0f),
        kIndicatorColor
    };
    // Spawned hidden: SHAPES_TO_TRIXEL skips a shape without SHAPE_FLAG_VISIBLE,
    // so the marker costs one archetype row and no pixels until a drag shows it.
    marker.flags_ = IRRender::SHAPE_FLAG_XRAY_OCCLUDED;
    // Resolve the canvas at RENDER time (SHAPES_TO_TRIXEL's kNullEntity
    // fallback) instead of keeping `C_ShapeDescriptor`'s ctor snapshot of the
    // active canvas. The snapshot is right for a shape built during scene
    // setup, but this one is built mid-frame from a per-frame system (the drag
    // path's `CAMERA_MOUSE_ROTATE` tick), where "the active canvas" is whatever
    // the last pass happened to leave set.
    marker.canvasEntity_ = IREntity::kNullEntity;
    return IREntity::createEntity(
        IRComponents::C_LocalTransform{IRMath::vec3(0.0f)},
        marker,
        IRComponents::C_Name{"CursorPivotIndicator"}
    );
}

// Move the marker to @p worldPos and reveal it. Three foreign-entity component
// reaches per drag start — edge-triggered (drag start/end only), not the
// per-entity-tick footgun. `VOXEL_PICKING` does the equivalent write without
// any `getComponent` because the highlight is its OWN iterating entity, so the
// transforms arrive as template params; the caller here holds only an id.
//
// `C_LocalTransform` AND `C_WorldTransform` are written together, for the same
// reason `VOXEL_PICKING` does it (`system_voxel_picking.hpp:29-32`): the drag
// path (`System<CAMERA_MOUSE_ROTATE>`) ticks in RENDER, by which point UPDATE's
// `PROPAGATE_TRANSFORM` has already run this frame — writing only the local
// transform would leave `SHAPES_TO_TRIXEL` (a `C_WorldTransform` reader) drawing
// the marker at its stale prior-frame position for the first frame of every
// show/reposition. The indicator is parentless, so world == local is the correct
// composition.
inline void showIndicator(IREntity::EntityId indicator, IRMath::vec3 worldPos) {
    if (indicator == IREntity::kNullEntity) {
        return;
    }
    IREntity::getComponent<IRComponents::C_LocalTransform>(indicator).translation_ = worldPos;
    IREntity::getComponent<IRComponents::C_WorldTransform>(indicator).translation_ = worldPos;
    auto &marker = IREntity::getComponent<IRComponents::C_ShapeDescriptor>(indicator);
    marker.flags_ |= IRRender::SHAPE_FLAG_VISIBLE;
}

// Take the marker back down. Position is left where it was — nothing renders
// it while the visible bit is clear, and the next drag overwrites it anyway.
inline void hideIndicator(IREntity::EntityId indicator) {
    if (indicator == IREntity::kNullEntity) {
        return;
    }
    IREntity::getComponent<IRComponents::C_ShapeDescriptor>(indicator).flags_ &=
        ~IRRender::SHAPE_FLAG_VISIBLE;
}

} // namespace IRPrefab::CursorPivot

#endif /* IR_PREFAB_CURSOR_PIVOT_H */
