#ifndef IR_PREFAB_GIZMO_H
#define IR_PREFAB_GIZMO_H

// Editor gizmo primitives — geometry + interaction wiring.
//
// Each builder spawns a small group of ECS entities, each carrying a
// `C_ShapeDescriptor` (rendered by SHAPES_TO_TRIXEL) and a
// `C_GizmoHandle` marker for the interaction systems. Returns the root
// entity of the group so callers can re-parent or destroy as a unit.
//
// Phase 1 renders handles at fixed world-space size. Phase 2 (T-164)
// adds the screen-space sizing UPDATE system (`GIZMO_SCREEN_SPACE_SIZE`)
// and opts each handle into the generic `SHAPE_FLAG_XRAY_OCCLUDED`
// shader path so occluded handles still read as a faint silhouette.
// Phase 3 (T-165) wires hover detection + drag interaction via the
// `GIZMO_HOVER` / `GIZMO_DRAG` INPUT-pipeline systems; each spawned
// handle carries `baseColor_` (for the hover tint round-trip) and
// `anchorEntity_` (the entity whose `C_LocalTransform` drag mutates — the
// gizmo group parent for multi-handle gizmos, `kNullEntity` for
// single-marker handles that should be hoverable but not draggable).

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/render/components/component_gizmo_handle.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

namespace IRPrefab::Gizmo {

namespace detail {

// Phase 1 handle sizing — tuned to be visible against the voxel_editor's
// 16-unit axis indicators while staying small enough not to dominate.
constexpr float kArrowShaftRadius = 0.4f;
constexpr float kArrowShaftLength = 6.0f;
constexpr float kArrowHeadRadius = 1.0f;
constexpr float kArrowHeadLength = 2.0f;

constexpr float kRingMajorRadius = 5.0f;
constexpr float kRingMinorRadius = 0.4f;

constexpr float kScaleStickRadius = 0.35f;
constexpr float kScaleStickLength = 5.0f;
constexpr float kScaleCapSize = 1.0f;
constexpr float kScaleCenterSize = 1.4f;

constexpr float kJointMarkerRadius = 1.0f;
constexpr float kBindPointArmLength = 1.6f;
constexpr float kBindPointArmRadius = 0.25f;
constexpr float kIKMarkerBaseRadius = 1.1f;
constexpr float kIKMarkerHeight = 2.2f;

using IRComponents::C_GizmoHandle;
using IRComponents::C_LocalTransform;
using IRComponents::C_Name;
using IRComponents::C_ShapeDescriptor;
using IRComponents::GizmoAxis;
using IRComponents::GizmoKind;
using IRMath::Color;
using IRMath::vec3;
using IRMath::vec4;

inline Color axisColor(GizmoAxis axis) {
    switch (axis) {
    case GizmoAxis::X:
        return Color{220, 60, 60, 255};
    case GizmoAxis::Y:
        return Color{60, 200, 60, 255};
    case GizmoAxis::Z:
        return Color{80, 130, 220, 255};
    case GizmoAxis::NONE:
    default:
        return Color{220, 220, 220, 255};
    }
}

// Local-space offset of an axis handle's anchor relative to the gizmo
// origin, scaled by `distance`. The CYLINDER / CONE primitives are
// oriented along the Z axis in their local frame — placement positions
// the handle and the engine's iso projection keeps the geometry
// readable from any cardinal camera yaw.
inline vec3 axisOffset(GizmoAxis axis, float distance) {
    switch (axis) {
    case GizmoAxis::X:
        return vec3(distance, 0.0f, 0.0f);
    case GizmoAxis::Y:
        return vec3(0.0f, distance, 0.0f);
    case GizmoAxis::Z:
        return vec3(0.0f, 0.0f, distance);
    case GizmoAxis::NONE:
    default:
        return vec3(0.0f);
    }
}

inline IREntity::EntityId spawnHandle(
    IREntity::EntityId parent,
    GizmoKind kind,
    GizmoAxis axis,
    vec3 localPos,
    IRRender::ShapeType shapeType,
    vec4 shapeParams,
    Color color,
    const char *name
) {
    // Opt the handle's shape into the generic xray-occlusion shader path
    // so SHAPES_TO_TRIXEL emits the faint-silhouette alpha blend where
    // the handle sits behind closer geometry (T-164 Phase 2).
    C_ShapeDescriptor shapeDesc{shapeType, shapeParams, color};
    shapeDesc.flags_ = IRRender::SHAPE_FLAG_VISIBLE | IRRender::SHAPE_FLAG_XRAY_OCCLUDED;

    // Capture construction-time reference values on the handle for
    // downstream systems. Phase 2 screen-space sizing reads
    // `referenceParams_` / `referenceLocalPos_` as the unscaled baseline
    // and writes scaled copies back each UPDATE tick. `isAnchor_` is
    // true for single-entity markers whose own `C_LocalTransform` is the
    // world-space anchor (the editor writes it post-construction) —
    // Phase 2 then scales params but leaves pos alone. Phase 3 records
    // `baseColor_` for the hover-tint round-trip and `anchorEntity_` =
    // `parent` so drag mutations land on the gizmo group entity (so
    // every axis of a multi-handle gizmo shares one anchor). For
    // single-marker handles called with `parent == kNullEntity`, the
    // anchor is null — the handle is still hoverable but drag is a
    // no-op.
    const bool isAnchor = (parent == IREntity::kNullEntity);
    IREntity::EntityId handle = IREntity::createEntity(
        C_LocalTransform{localPos},
        shapeDesc,
        C_GizmoHandle{kind, axis, shapeParams, localPos, isAnchor, color, parent},
        C_Name{name}
    );
    if (!isAnchor) {
        IREntity::setParent(handle, parent);
    }
    return handle;
}

inline IREntity::EntityId makeGroup(IREntity::EntityId parent, const char *name) {
    IREntity::EntityId group = IREntity::createEntity(C_LocalTransform{vec3(0.0f)}, C_Name{name});
    if (parent != IREntity::kNullEntity) {
        IREntity::setParent(group, parent);
    }
    return group;
}

// Shared by createRotateGizmo / createRotateGizmoForAnchor: the three axis
// rings (TORUS per axis), parented to — and anchored at — `parent`.
inline void spawnRotateRings(IREntity::EntityId parent, const char *ringName) {
    for (GizmoAxis axis : {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z}) {
        spawnHandle(
            parent,
            GizmoKind::ROTATE_RING,
            axis,
            vec3(0.0f),
            IRRender::ShapeType::TORUS,
            vec4(kRingMajorRadius, kRingMinorRadius, 0.0f, 0.0f),
            axisColor(axis),
            ringName
        );
    }
}

// Shared by createTranslateGizmo / createTranslateGizmoForAnchor: the three
// axis arrows (CYLINDER shaft + CONE head per axis), parented to — and
// anchored at — `parent`.
inline void
spawnTranslateArrows(IREntity::EntityId parent, const char *shaftName, const char *headName) {
    constexpr float shaftCenter = kArrowShaftLength * 0.5f;
    constexpr float headCenter = kArrowShaftLength + kArrowHeadLength * 0.5f;

    for (GizmoAxis axis : {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z}) {
        const Color color = axisColor(axis);
        spawnHandle(
            parent,
            GizmoKind::TRANSLATE_ARROW,
            axis,
            axisOffset(axis, shaftCenter),
            IRRender::ShapeType::CYLINDER,
            vec4(kArrowShaftRadius, 0.0f, kArrowShaftLength, 0.0f),
            color,
            shaftName
        );
        spawnHandle(
            parent,
            GizmoKind::TRANSLATE_ARROW,
            axis,
            axisOffset(axis, headCenter),
            IRRender::ShapeType::CONE,
            vec4(kArrowHeadRadius, 0.0f, kArrowHeadLength, 0.0f),
            color,
            headName
        );
    }
}

} // namespace detail

/// Translate gizmo — three axis arrows (CYLINDER shaft + CONE head).
/// Returns the group entity; the three arrow shafts + heads are children.
inline IREntity::EntityId createTranslateGizmo(IREntity::EntityId parent = IREntity::kNullEntity) {
    using namespace detail;
    IREntity::EntityId group = makeGroup(parent, "GizmoTranslate");
    spawnTranslateArrows(group, "GizmoTranslateShaft", "GizmoTranslateHead");
    return group;
}

/// Translate gizmo whose three axis arrows drag the given `anchor`
/// entity's own `C_LocalTransform` (rather than a separate gizmo group).
/// Each arrow is parented to `anchor` so it tracks the anchor as it
/// moves, and its `C_GizmoHandle::anchorEntity_` points at `anchor` so a
/// `GIZMO_DRAG` mutates the anchor directly. Use for per-target placement
/// handles where the moved entity IS the visual anchor — e.g. a skeletal
/// joint authored in the voxel editor (#1604). No group entity is
/// created; the returned id is `anchor` itself. (`createTranslateGizmo`,
/// by contrast, anchors its arrows to the group it returns, so dragging
/// moves the gizmo as a free-standing unit.)
inline IREntity::EntityId createTranslateGizmoForAnchor(IREntity::EntityId anchor) {
    using namespace detail;
    spawnTranslateArrows(anchor, "GizmoAnchorTranslateShaft", "GizmoAnchorTranslateHead");
    return anchor;
}

/// Rotate gizmo — three axis rings (TORUS per axis).
inline IREntity::EntityId createRotateGizmo(IREntity::EntityId parent = IREntity::kNullEntity) {
    using namespace detail;
    IREntity::EntityId group = makeGroup(parent, "GizmoRotate");
    spawnRotateRings(group, "GizmoRotateRing");
    return group;
}

/// Rotate gizmo whose three axis rings rotate the given `anchor` entity's
/// own `C_LocalTransform::rotation_` (rather than a separate gizmo group).
/// Each ring is parented to `anchor` so it tracks the anchor as it moves,
/// and its `C_GizmoHandle::anchorEntity_` points at `anchor` so a
/// `GIZMO_DRAG` rotation lands on the anchor directly. Use for per-target
/// posing handles where the rotated entity IS the visual anchor — e.g. FK
/// pose editing of a skeletal joint in the voxel editor (#1610). No group
/// entity is created; the returned id is `anchor` itself.
inline IREntity::EntityId createRotateGizmoForAnchor(IREntity::EntityId anchor) {
    using namespace detail;
    spawnRotateRings(anchor, "GizmoAnchorRotateRing");
    return anchor;
}

/// Scale gizmo — three axis sticks with terminal caps + a center cube
/// for uniform scale.
inline IREntity::EntityId createScaleGizmo(IREntity::EntityId parent = IREntity::kNullEntity) {
    using namespace detail;
    IREntity::EntityId group = makeGroup(parent, "GizmoScale");

    constexpr float stickCenter = kScaleStickLength * 0.5f;
    constexpr float capCenter = kScaleStickLength + kScaleCapSize * 0.5f;

    for (GizmoAxis axis : {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z}) {
        const Color color = axisColor(axis);
        spawnHandle(
            group,
            GizmoKind::SCALE_STICK,
            axis,
            axisOffset(axis, stickCenter),
            IRRender::ShapeType::CYLINDER,
            vec4(kScaleStickRadius, 0.0f, kScaleStickLength, 0.0f),
            color,
            "GizmoScaleStick"
        );
        spawnHandle(
            group,
            GizmoKind::SCALE_STICK,
            axis,
            axisOffset(axis, capCenter),
            IRRender::ShapeType::BOX,
            vec4(kScaleCapSize, kScaleCapSize, kScaleCapSize, 0.0f),
            color,
            "GizmoScaleCap"
        );
    }

    spawnHandle(
        group,
        GizmoKind::SCALE_CENTER,
        GizmoAxis::NONE,
        vec3(0.0f),
        IRRender::ShapeType::BOX,
        vec4(kScaleCenterSize, kScaleCenterSize, kScaleCenterSize, 0.0f),
        Color{220, 220, 220, 255},
        "GizmoScaleCenter"
    );
    return group;
}

/// Joint marker — solid sphere at a skeletal joint.
inline IREntity::EntityId createJointMarker(IREntity::EntityId parent = IREntity::kNullEntity) {
    using namespace detail;
    return spawnHandle(
        parent,
        GizmoKind::JOINT_MARKER,
        GizmoAxis::NONE,
        vec3(0.0f),
        IRRender::ShapeType::SPHERE,
        vec4(kJointMarkerRadius, 0.0f, 0.0f, 0.0f),
        Color{255, 180, 60, 255},
        "GizmoJointMarker"
    );
}

/// Bind-point marker — three orthogonal cylinder arms forming a cross.
/// A label (`C_TextSegment`) for the bind-point name can be attached by
/// the caller; this builder only emits the geometric primitive.
inline IREntity::EntityId createBindPointMarker(IREntity::EntityId parent = IREntity::kNullEntity) {
    using namespace detail;
    IREntity::EntityId group = makeGroup(parent, "GizmoBindPoint");

    const Color color = Color{200, 100, 255, 255};
    spawnHandle(
        group,
        GizmoKind::BIND_POINT_MARKER,
        GizmoAxis::X,
        vec3(0.0f),
        IRRender::ShapeType::BOX,
        vec4(kBindPointArmLength, kBindPointArmRadius, kBindPointArmRadius, 0.0f),
        color,
        "GizmoBindPointArmX"
    );
    spawnHandle(
        group,
        GizmoKind::BIND_POINT_MARKER,
        GizmoAxis::Y,
        vec3(0.0f),
        IRRender::ShapeType::BOX,
        vec4(kBindPointArmRadius, kBindPointArmLength, kBindPointArmRadius, 0.0f),
        color,
        "GizmoBindPointArmY"
    );
    spawnHandle(
        group,
        GizmoKind::BIND_POINT_MARKER,
        GizmoAxis::Z,
        vec3(0.0f),
        IRRender::ShapeType::BOX,
        vec4(kBindPointArmRadius, kBindPointArmRadius, kBindPointArmLength, 0.0f),
        color,
        "GizmoBindPointArmZ"
    );
    return group;
}

/// IK target marker — distinctive shape (cone, yellow). Phase 1 uses a
/// cone as the "distinctive" primitive; Phase 2 may upgrade to a true
/// tetrahedron once a 4-wedge composition or a new SDF primitive lands.
inline IREntity::EntityId createIKMarker(IREntity::EntityId parent = IREntity::kNullEntity) {
    using namespace detail;
    return spawnHandle(
        parent,
        GizmoKind::IK_MARKER,
        GizmoAxis::NONE,
        vec3(0.0f),
        IRRender::ShapeType::CONE,
        vec4(kIKMarkerBaseRadius, 0.0f, kIKMarkerHeight, 0.0f),
        IRMath::IRColors::kYellow,
        "GizmoIKMarker"
    );
}

} // namespace IRPrefab::Gizmo

#endif /* IR_PREFAB_GIZMO_H */
