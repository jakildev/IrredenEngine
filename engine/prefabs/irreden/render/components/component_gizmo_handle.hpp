#ifndef COMPONENT_GIZMO_HANDLE_H
#define COMPONENT_GIZMO_HANDLE_H

#include <cstdint>

namespace IRComponents {

/// Per-handle metadata for editor gizmo entities. The visible geometry is
/// supplied by a sibling `C_ShapeDescriptor` on the same entity; this
/// component is a marker that tags which gizmo + which axis + whether the
/// mouse is currently hovering the handle (Phase 3 will read `hover_`).
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

    C_GizmoHandle() = default;

    C_GizmoHandle(GizmoKind kind, GizmoAxis axis)
        : kind_{kind}
        , axis_{axis} {}
};

} // namespace IRComponents

#endif /* COMPONENT_GIZMO_HANDLE_H */
