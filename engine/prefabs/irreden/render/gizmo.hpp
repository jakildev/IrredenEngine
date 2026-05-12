#ifndef IR_PREFAB_GIZMO_H
#define IR_PREFAB_GIZMO_H

// Editor gizmo primitives — Phase 1 (geometry only).
//
// Each builder spawns a small group of ECS entities, each carrying a
// `C_ShapeDescriptor` (rendered by SHAPES_TO_TRIXEL) and a
// `C_GizmoHandle` marker for the upcoming interaction work. Returns the
// root entity of the group so callers can re-parent or destroy as a unit.
//
// Phase 1 renders handles at fixed world-space size. Phase 2 (T-164) adds
// the screen-space sizing UPDATE system (`GIZMO_SCREEN_SPACE_SIZE`) and the
// depth-aware-dimming `SHAPE_FLAG_GIZMO` shader path. Phase 3 will wire
// hover detection + drag interaction once T-153 (mouse picking) lands.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_position_3d.hpp>
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
using IRComponents::C_Name;
using IRComponents::C_Position3D;
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
    // Tag the shape descriptor with SHAPE_FLAG_GIZMO so the SHAPES_TO_TRIXEL
    // shader knows to emit the faint-silhouette alpha blend where the handle
    // sits behind world geometry (T-164 Phase 2).
    C_ShapeDescriptor shapeDesc{shapeType, shapeParams, color};
    shapeDesc.flags_ = IRRender::SHAPE_FLAG_VISIBLE | IRRender::SHAPE_FLAG_GIZMO;

    // Capture the construction-time reference values on the handle. The
    // Phase 2 screen-space sizing system reads `referenceParams_` /
    // `referenceLocalPos_` as the unscaled baseline and writes
    // (baseline × pixelSize / zoom) back to the sibling components each
    // UPDATE tick. `isAnchor_` is true for single-entity markers whose
    // own `C_Position3D` is the world-space anchor (the editor writes it
    // post-construction) — Phase 2 then scales params but leaves pos
    // alone so the editor placement isn't clobbered.
    const bool isAnchor = (parent == IREntity::kNullEntity);
    IREntity::EntityId handle = IREntity::createEntity(
        C_Position3D{localPos},
        shapeDesc,
        C_GizmoHandle{kind, axis, shapeParams, localPos, isAnchor},
        C_Name{name}
    );
    if (!isAnchor) {
        IREntity::setParent(handle, parent);
    }
    return handle;
}

inline IREntity::EntityId makeGroup(IREntity::EntityId parent, const char *name) {
    IREntity::EntityId group = IREntity::createEntity(C_Position3D{vec3(0.0f)}, C_Name{name});
    if (parent != IREntity::kNullEntity) {
        IREntity::setParent(group, parent);
    }
    return group;
}

} // namespace detail

/// Translate gizmo — three axis arrows (CYLINDER shaft + CONE head).
/// Returns the group entity; the three arrow shafts + heads are children.
inline IREntity::EntityId createTranslateGizmo(IREntity::EntityId parent = IREntity::kNullEntity) {
    using namespace detail;
    IREntity::EntityId group = makeGroup(parent, "GizmoTranslate");

    constexpr float shaftCenter = kArrowShaftLength * 0.5f;
    constexpr float headCenter = kArrowShaftLength + kArrowHeadLength * 0.5f;

    for (GizmoAxis axis : {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z}) {
        const Color color = axisColor(axis);
        spawnHandle(
            group,
            GizmoKind::TRANSLATE_ARROW,
            axis,
            axisOffset(axis, shaftCenter),
            IRRender::ShapeType::CYLINDER,
            vec4(kArrowShaftRadius, 0.0f, kArrowShaftLength, 0.0f),
            color,
            "GizmoTranslateShaft"
        );
        spawnHandle(
            group,
            GizmoKind::TRANSLATE_ARROW,
            axis,
            axisOffset(axis, headCenter),
            IRRender::ShapeType::CONE,
            vec4(kArrowHeadRadius, 0.0f, kArrowHeadLength, 0.0f),
            color,
            "GizmoTranslateHead"
        );
    }
    return group;
}

/// Rotate gizmo — three axis rings (TORUS per axis).
inline IREntity::EntityId createRotateGizmo(IREntity::EntityId parent = IREntity::kNullEntity) {
    using namespace detail;
    IREntity::EntityId group = makeGroup(parent, "GizmoRotate");

    for (GizmoAxis axis : {GizmoAxis::X, GizmoAxis::Y, GizmoAxis::Z}) {
        spawnHandle(
            group,
            GizmoKind::ROTATE_RING,
            axis,
            vec3(0.0f),
            IRRender::ShapeType::TORUS,
            vec4(kRingMajorRadius, kRingMinorRadius, 0.0f, 0.0f),
            axisColor(axis),
            "GizmoRotateRing"
        );
    }
    return group;
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
