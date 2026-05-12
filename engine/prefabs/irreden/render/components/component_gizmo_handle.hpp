#ifndef COMPONENT_GIZMO_HANDLE_H
#define COMPONENT_GIZMO_HANDLE_H

#include <cstdint>

#include <irreden/ir_math.hpp>

namespace IRComponents {

/// Per-handle metadata for editor gizmo entities. The visible geometry is
/// supplied by a sibling `C_ShapeDescriptor` on the same entity; this
/// component is a marker that tags which gizmo + which axis + whether the
/// mouse is currently hovering the handle (Phase 3 will read `hover_`).
///
/// `referenceParams_` and `referenceLocalPos_` capture the construction-time
/// values written by the Phase 1 builders. The Phase 2 screen-space sizing
/// system (T-164) reads these as the unscaled baseline and writes scaled
/// copies back to `C_ShapeDescriptor::params_` (and the sibling
/// `C_Position3D::pos_` when `isAnchor_` is false) each UPDATE tick — so
/// multiple frames of scaling don't compound. Phase 3 will use the same
/// baseline for hover / drag.
///
/// `isAnchor_` distinguishes single-entity gizmo markers (joint, IK) — whose
/// own `C_Position3D` IS the world-space anchor the caller wrote — from
/// child handles inside a group (translate arrows, rotate rings, scale
/// sticks/center) — whose `C_Position3D` is a local offset relative to a
/// group-root parent. Phase 2 scales `params_` for both forms, but only
/// scales `pos_` for child handles, so the anchor's editor-placed world
/// position isn't clobbered.
///
/// Phase 1 (F-0.5) does not yet wire hover or drag — `hover_` defaults to
/// false and the field is reserved for the upcoming mouse-picking pass.

enum class GizmoKind : std::uint8_t {
    TRANSLATE_ARROW,   ///< Cylinder shaft + cone head along an axis.
    ROTATE_RING,       ///< Torus around an axis.
    SCALE_STICK,       ///< Cylinder stick + box cap along an axis.
    SCALE_CENTER,      ///< Center box for uniform scale.
    JOINT_MARKER,      ///< Sphere at a skeletal joint.
    BIND_POINT_MARKER, ///< Small cross at a bind-point.
    IK_MARKER,         ///< Cone (Phase 1) / tetrahedron (Phase 2) at an IK target.
};

enum class GizmoAxis : std::uint8_t {
    NONE,
    X,
    Y,
    Z,
};

struct C_GizmoHandle {
    GizmoKind kind_ = GizmoKind::JOINT_MARKER;
    GizmoAxis axis_ = GizmoAxis::NONE;
    bool hover_ = false;
    bool isAnchor_ = true;
    IRMath::vec4 referenceParams_ = IRMath::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    IRMath::vec3 referenceLocalPos_ = IRMath::vec3(0.0f);

    C_GizmoHandle() = default;

    C_GizmoHandle(GizmoKind kind, GizmoAxis axis)
        : kind_{kind}
        , axis_{axis} {}

    C_GizmoHandle(
        GizmoKind kind,
        GizmoAxis axis,
        IRMath::vec4 referenceParams,
        IRMath::vec3 referenceLocalPos,
        bool isAnchor
    )
        : kind_{kind}
        , axis_{axis}
        , isAnchor_{isAnchor}
        , referenceParams_{referenceParams}
        , referenceLocalPos_{referenceLocalPos} {}
};

} // namespace IRComponents

#endif /* COMPONENT_GIZMO_HANDLE_H */
